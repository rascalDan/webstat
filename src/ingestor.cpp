#include "ingestor.hpp"
#include "sql.hpp"
#include "uaLookup.hpp"
#include "util.hpp"
#include <connection.h>
#include <csignal>
#include <dbTypes.h>
#include <modifycommand.h>
#include <ranges>
#include <scn/scan.h>
#include <selectcommand.h>
#include <selectcommandUtil.impl.h>
#include <syslog.h>
#include <utility>
#include <zlib.h>

namespace DB {
	template<>
	void
	// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
	DB::Command::bindParam(unsigned int idx, const WebStat::Entity & entity)
	{
		bindParamI(idx, entity.id);
	}
}

namespace WebStat {
	namespace {
		using ByteArrayView = std::span<const uint8_t>;

		auto
		bytesToHexRange(const ByteArrayView bytes)
		{
			constexpr auto HEXN = 16ZU;
			return bytes | std::views::transform([](auto byte) {
				return std::array {byte / HEXN, byte % HEXN};
			}) | std::views::join
					| std::views::transform([](auto nibble) {
						  return "0123456789abcdef"[nibble];
					  });
		}

		EntityHash
		makeHash(const std::string_view value)
		{
			MD5_CTX ctx {};
			MD5Init(&ctx);
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - correct for md5ing raw bytes
			MD5Update(&ctx, reinterpret_cast<const uint8_t *>(value.data()), value.length());
			EntityHash hash {};
			MD5Final(hash.data(), &ctx);
			return hash;
		}

		template<EntityType Type> struct ToEntity {
			Entity
			operator()(const std::string_view value) const
			{
				return {
						.hash = makeHash(value),
						.id = std::nullopt,
						.type = Type,
						.value = value,
				};
			}

			template<typename T>
			std::optional<Entity>
			operator()(const std::optional<T> & value) const
			{
				return value.transform([this](auto && contained) {
					return (*this)(contained);
				});
			}
		};

		auto
		hashScanValues(const Ingestor::ScanValues & values)
		{
			static constexpr std::tuple<ToEntity<EntityType::VirtualHost>, std::identity, std::identity, std::identity,
					ToEntity<EntityType::Path>, ToEntity<EntityType::QueryString>, std::identity, std::identity,
					std::identity, std::identity, ToEntity<EntityType::Referrer>, ToEntity<EntityType::UserAgent>,
					ToEntity<EntityType::ContentType>>
					ENTITY_TYPE_MAP;
			static constexpr size_t VALUE_COUNT = std::tuple_size_v<Ingestor::ScanValues>;
			static_assert(VALUE_COUNT == std::tuple_size_v<decltype(ENTITY_TYPE_MAP)>);

			return [&values]<size_t... N>(std::index_sequence<N...>) {
				return std::make_tuple(std::get<N>(ENTITY_TYPE_MAP)(std::get<N>(values))...);
			}(std::make_index_sequence<VALUE_COUNT>());
		}

		template<std::integral T = EntityId, typename... Binds>
		T
		insert(auto && dbconn, const std::string & sql, const DB::CommandOptionsPtr & opts, Binds &&... binds)
		{
			auto ins = dbconn->select(sql, opts);
			bindMany(ins, 0, std::forward<Binds>(binds)...);
			if (ins->fetch()) {
				T out;
				(*ins)[0] >> out;
				return out;
			}
			throw DB::NoRowsAffected {};
		}

		template<typename... Fields, typename... Binds>
			requires(sizeof...(Fields) > 1)
		std::tuple<Fields...>
		insert(auto && dbconn, const std::string & sql, const DB::CommandOptionsPtr & opts, Binds &&... binds)
		{
			DB::SelectCommandPtr ins = dbconn->select(sql, opts);
			bindMany(ins, 0, std::forward<Binds>(binds)...);
			for (auto row : ins->template as<Fields...>()) {
				return [&row]<std::unsigned_integral T, T... N>(std::integer_sequence<T, N...>) {
					return std::make_tuple(row.template get<N>()...);
				}(std::make_integer_sequence<unsigned int, sizeof...(Fields)> {});
			}
			throw DB::NoRowsAffected {};
		}
	}

	Ingestor * Ingestor::currentIngestor = nullptr;

	Ingestor::Ingestor(const utsname & host, IngestorSettings givenSettings) :
		Ingestor {host,
				std::make_shared<DB::ConnectionPool>(
						givenSettings.dbMax, givenSettings.dbKeep, givenSettings.dbType, givenSettings.dbConnStr),
				std::move(givenSettings)}
	{
	}

	Ingestor::Ingestor(const utsname & host, DB::ConnectionPoolPtr dbpl, IngestorSettings givenSettings) :
		settings {std::move(givenSettings)}, dbpool {std::move(dbpl)},
		handleCompleteCurlOps {&Ingestor::jobHandleCompleteCurlOps, &Ingestor::haveCurlOperations},
		ingestParkedLines {&Ingestor::jobReadParkedLines}, purgeOldLogs {&Ingestor::jobPurgeOldLogs},
		storeQueueLines {&Ingestor::jobStoreQueuedLines},
		hostnameId {insert(dbpool->get(), SQL::HOST_UPSERT, SQL::HOST_UPSERT_OPTS, host.nodename, host.sysname,
				host.release, host.version, host.machine, host.domainname)},
		curl {curl_multi_init()}
	{
		assert(!currentIngestor);
		currentIngestor = this;
		signal(SIGTERM, &sigtermHandler);
		signal(SIGUSR1, &sigusr1Handler);
		signal(SIGUSR2, &sigusr2Handler);
		queuedLines.reserve(settings.maxBatchSize);
	}

	Ingestor::~Ingestor()
	{
		assert(currentIngestor);
		signal(SIGTERM, SIG_DFL);
		signal(SIGUSR1, SIG_DFL);
		signal(SIGUSR2, SIG_DFL);
		currentIngestor = nullptr;
	}

	void
	Ingestor::sigtermHandler(int sigNo)
	{
		assert(currentIngestor);
		currentIngestor->terminate(sigNo);
	}

	void
	Ingestor::terminate(int sigNo)
	{
		log(LOG_NOTICE, "Caught sig %d, terminating", sigNo);
		terminated = true;
		curl_multi_wakeup(curl.get());
	}

	void
	Ingestor::sigusr1Handler(int)
	{
		assert(currentIngestor);
		currentIngestor->logStats();
	}

	void
	Ingestor::sigusr2Handler(int)
	{
		assert(currentIngestor);
		currentIngestor->clearStats();
	}

	Ingestor::ScanResult
	Ingestor::scanLogLine(std::string_view input)
	{
		return scn::scan< // Field : Apache format specifier : example
				std::string_view, // virtual_host : %V : some.host.name
				std::string_view, // remoteip : %a : 1.2.3.4 (or ipv6)
				uint64_t, // request_time : %{usec}t : 123456790
				std::string_view, // method : %m : GET
				QuotedString, // path : "%u" : "/foo/bar"
				QueryString, // query_string : "%q" : "?query=string" or ""
				std::string_view, // protocol : %r : HTTPS/2.0
				unsigned short, // status : %>s : 200
				unsigned int, // size : %B : 1234
				unsigned int, // duration : %D : 1234
				CLFString, // referrer : "%{Referer}i" : "https://google.com/whatever" or "-"
				CLFString, // user_agent : "%{User-agent}i" : "Chromium v123.4" or "-"
				CLFString // content_type : "%{Content-type}o" : "test/plain" or "-"
				>(input, R"({} {} {} {:[A-Z]} {} {} {} {} {} {} {} {} {})");
	}

	auto
	Ingestor::withCurlLock(auto &&... operation)
	{
		std::lock_guard curlOperationsLock {curlOperationsMutex};
		return std::invoke(std::forward<decltype(operation)>(operation)...);
	}

	bool
	Ingestor::haveCurlOperations()
	{
		return withCurlLock([this]() {
			return !curlOperations.empty();
		});
	}

	void
	Ingestor::handleCurlOperations()
	{
		if (!curlOperations.empty()) {
			curl_multi_perform(curl.get(), nullptr);
		}
	}

	Ingestor::Job::Result
	Ingestor::jobHandleCompleteCurlOps()
	{
		int remaining {};
		while (auto msg = withCurlLock(curl_multi_info_read, curl.get(), &remaining)) {
			if (msg->msg == CURLMSG_DONE) {
				const auto result = msg->data.result;
				if (auto operation = withCurlLock([this, handle = msg->easy_handle]() {
						curl_multi_remove_handle(curl.get(), handle);
						return curlOperations.extract(handle);
					})) {
					std::invoke((result == CURLE_OK) ? &CurlOperation::whenComplete : &CurlOperation::onError,
							operation.mapped(), dbpool->get().get());
				}
				else {
					log(LOG_WARNING, "Failed to lookup CurlOperation");
				}
			}
		}
		return []() {
			return 0;
		};
	}

	void
	Ingestor::ingestLog(std::FILE * input)
	{
		curl_waitfd logIn {.fd = fileno(input), .events = CURL_WAIT_POLLIN, .revents = 0};

		const auto curlTimeOut = static_cast<int>(
				std::chrono::duration_cast<std::chrono::milliseconds>(settings.checkJobsAfter).count());
		while (!terminated && curl_multi_poll(curl.get(), &logIn, 1, curlTimeOut, nullptr) == CURLM_OK) {
			if (logIn.revents) {
				if (auto line = scn::scan<std::string>(input, "{:[^\n]}\n")) {
					stats.linesRead++;
					queuedLines.emplace_back(std::move(line->value()));
					if (queuedLines.size() >= settings.maxBatchSize) {
						beginIngestQueuedLogLines();
					}
				}
				else {
					break;
				}
			}
			else {
				beginIngestQueuedLogLines();
			}
			if (expiredThenSet(lastCheckedJobs, settings.checkJobsAfter)) {
				runJobsAsNeeded();
			}
			withCurlLock(&Ingestor::handleCurlOperations, this);
		}
		finishAllJobs();
		storeQueueLines.currentRun.reset();
		beginIngestQueuedLogLines();
		storeQueueLines.currentRun.reset();
		std::ignore = parkLogLines(queuedLines);
		std::ignore = parkLogLines(processingLines);
		withCurlLock([this]() {
			if (!curlOperations.empty()) {
				int running = -1;
				while (running && curl_multi_poll(curl.get(), nullptr, 0, 500, &running) == CURLM_OK) {
					curl_multi_perform(curl.get(), nullptr);
				}
			}
		});
		jobHandleCompleteCurlOps();
		logStats();
	}

	std::pair<std::future<Ingestor::Job::Result> &, bool>
	Ingestor::beginIngestQueuedLogLines()
	{
		if (storeQueueLines.currentRun) {
			if (storeQueueLines.currentRun->wait_for(std::chrono::seconds {}) != std::future_status::ready) {
				return {*storeQueueLines.currentRun, false};
			}
			finalizeJob(storeQueueLines, {}, Job::LastRunTime::clock::now());
		}
		if (processingLines.empty()) {
			std::swap(queuedLines, processingLines);
		}
		return {storeQueueLines.currentRun.emplace(std::async(std::launch::async, storeQueueLines.impl, this)), true};
	}

	Ingestor::Job::Result
	Ingestor::jobStoreQueuedLines()
	{
		auto storedEnd = processingLines.begin();
		try {
			for (auto batch : processingLines | std::views::chunk(settings.maxBatchSize)) {
				ingestLogLines(dbpool->get().get(), batch);
				storedEnd = batch.end();
			}
		}
		catch (const std::exception & excp) {
			log(LOG_ERR, "Unhandled exception: %s, clearing known entity list", excp.what());
			existingEntities.clear();
		}
		auto count = std::distance(processingLines.begin(), storedEnd);
		processingLines.erase(processingLines.begin(), storedEnd);
		return [count]() {
			return count;
		};
	}

	template<typename... T>
	std::vector<Entity *>
	Ingestor::entities(std::tuple<T...> & values)
	{
		std::vector<Entity *> entities;
		visit(
				[&entities]<typename V>(V & value) {
					static_assert(!std::is_const_v<V>);
					if constexpr (std::is_same_v<V, Entity>) {
						entities.emplace_back(&value);
					}
					else if constexpr (std::is_same_v<V, std::optional<Entity>>) {
						if (value) {
							entities.emplace_back(&*value);
						}
					}
				},
				values);
		return entities;
	}

	void
	Ingestor::ingestLogLines(DB::Connection * dbconn, const LinesView lines)
	{
		auto entityIds = std::views::transform([](auto && value) {
			return std::make_pair(value->hash, *value->id);
		});

		DB::TransactionScope batchTx {*dbconn};
		for (const auto & line : lines) {
			if (auto result = scanLogLine(line)) {
				stats.linesParsed++;
				auto values = hashScanValues(result->values());
				auto valuesEntities = entities(values);
				fillKnownEntities(valuesEntities);
				try {
					DB::TransactionScope lineTx {*dbconn};
					storeNewEntities(dbconn, valuesEntities);
					existingEntities.insert_range(valuesEntities | entityIds);
					storeLogLine(dbconn, values);
				}
				catch (const DB::Error & originalError) {
					try {
						DB::TransactionScope lineTx {*dbconn};
						auto uninsertableLine = ToEntity<EntityType::UninsertableLine> {}(line);
						storeNewEntity(dbconn, uninsertableLine);
						log(LOG_NOTICE,
								"Failed to store parsed line and/or associated entties, but did store raw line, %u:%s",
								*uninsertableLine.id, line.c_str());
					}
					catch (const std::exception & excp) {
						log(LOG_NOTICE, "Failed to store line in any form, DB connection lost? %s", excp.what());
						throw originalError;
					}
				}
			}
			else {
				stats.linesParseFailed++;
				auto unparsableLine = ToEntity<EntityType::UnparsableLine> {}(line);
				storeNewEntity(dbconn, unparsableLine);
				log(LOG_NOTICE, "Failed to parse line, this is a bug: %u:%s", *unparsableLine.id, line.c_str());
			}
		}
	}

	std::expected<std::filesystem::path, int>
	Ingestor::parkLogLines(LineBatch & lines)
	{
		if (lines.empty()) {
			return std::unexpected(0);
		}
		const std::filesystem::path path {
				settings.fallbackDir / std::format("parked-{:s}.short", bytesToHexRange(makeHash(lines.front())))};
		if (auto parked = FilePtr(fopen(path.c_str(), "w"))) {
			fprintf(parked.get(), "%zu\n", lines.size());
			for (const auto & line : lines) {
				fprintf(parked.get(), "%.*s\n", static_cast<int>(line.length()), line.data());
			}
			if (fflush(parked.get()) == 0) {
				lines.clear();
				auto finalPath = std::filesystem::path {path}.replace_extension(".log");
				parked.reset();
				if (rename(path.c_str(), finalPath.c_str()) == 0) {
					return finalPath;
				}
			}
		}
		const int err = errno;
		log(LOG_ERR, "Failed to park %zu queued lines:", lines.size());
		for (const auto & line : lines) {
			log(LOG_ERR, "\t%.*s", static_cast<int>(line.length()), line.data());
		}
		return std::unexpected(err);
	}

	void
	Ingestor::finalizeJob(Job & job, const std::chrono::minutes freq, const Job::LastRunTime::clock::time_point now)
	{
		try {
			job.currentRun->get()();
			job.lastRun = now;
		}
		catch (const std::exception & excp) {
			log(LOG_ERR, "Job run failed: %s", excp.what());
			// Error, retry in half the frequency
			job.lastRun = now - (freq / 2);
		}
		job.currentRun.reset();
	}

	void
	Ingestor::runJobsAsNeeded()
	{
		auto runJobAsNeeded = [this, now = Job::LastRunTime::clock::now()](Job & job, auto freq) {
			if (job.currentRun) {
				if (job.currentRun->wait_for(std::chrono::seconds {}) == std::future_status::ready) {
					finalizeJob(job, freq, now);
				}
			}
			else if (expired(job.lastRun, freq, now)) {
				if (!job.cond || std::invoke(job.cond, this)) {
					job.currentRun.emplace(std::async(std::launch::async, job.impl, this));
				}
			}
		};
		runJobAsNeeded(handleCompleteCurlOps, std::chrono::minutes {1});
		runJobAsNeeded(ingestParkedLines, settings.freqIngestParkedLines);
		runJobAsNeeded(purgeOldLogs, settings.freqPurgeOldLogs);
	}

	void
	Ingestor::finishAllJobs()
	{
		auto finishJob = [this, now = Job::LastRunTime::clock::now()](Job & job) {
			if (job.currentRun) {
				job.currentRun->wait();
				finalizeJob(job, {}, now);
			}
		};
		finishJob(ingestParkedLines);
		finishJob(purgeOldLogs);
		finishJob(storeQueueLines);
		finishJob(handleCompleteCurlOps);
	}

	Ingestor::Job::Result
	Ingestor::jobReadParkedLines()
	{
		for (auto pathIter = std::filesystem::directory_iterator {settings.fallbackDir};
				pathIter != std::filesystem::directory_iterator {}; ++pathIter) {
			if (scn::scan<std::string>(pathIter->path().filename().string(), "parked-{:[a-zA-Z0-9]}.log")) {
				return [lines = jobReadParkedLines(pathIter->path()), this, path = pathIter->path()]() mutable {
					auto count = lines.size();
					queuedLines.append_range(std::move(lines));
					unlink(path.c_str());
					return count;
				};
			}
		}
		return []() {
			return 0;
		};
	}

	Ingestor::LineBatch
	Ingestor::jobReadParkedLines(const std::filesystem::path & path)
	{
		if (auto parked = FilePtr(fopen(path.c_str(), "r"))) {
			if (auto count = scn::scan<size_t>(parked.get(), "{}\n")) {
				try {
					return jobReadParkedLines(parked.get(), count->value());
				}
				catch (...) {
					auto failPath = auto {path}.replace_extension(".short");
					rename(path.c_str(), failPath.c_str());
					throw;
				}
			}
		}
		throw std::system_error {errno, std::generic_category(), strerror(errno)};
	}

	Ingestor::LineBatch
	Ingestor::jobReadParkedLines(FILE * lines, size_t count)
	{
		LineBatch parkedLines;
		parkedLines.reserve(count);
		for (size_t lineNo = 0; lineNo < count; ++lineNo) {
			if (auto line = scn::scan<std::string>(lines, "{:[^\n]}\n")) {
				stats.linesRead++;
				parkedLines.emplace_back(std::move(line->value()));
			}
			else {
				throw std::system_error {errno, std::generic_category(), "Short read of parked file"};
			}
		}
		return parkedLines;
	}

	Ingestor::Job::Result
	Ingestor::jobPurgeOldLogs()
	{
		auto dbconn = dbpool->get();
		const auto stopAt = Job::LastRunTime::clock::now() + settings.purgeDeleteMaxTime;
		const auto purge = dbconn->modify(SQL::ACCESS_LOG_PURGE_OLD, SQL::ACCESS_LOG_PURGE_OLD_OPTS);
		purge->bindParam(0, std::format("{} days", settings.purgeDaysToKeep));
		purge->bindParam(1, settings.purgeDeleteMax);
		unsigned int purgedTotal {};
		while (!terminated && stopAt > Job::LastRunTime::clock::now()) {
			const auto purged = purge->execute();
			purgedTotal += purged;
			if (purged < settings.purgeDeleteMax) {
				break;
			}
			std::this_thread::sleep_for(settings.purgeDeletePause);
		}
		return [purgedTotal]() {
			return purgedTotal;
		};
	}

	void
	Ingestor::fillKnownEntities(const std::span<Entity *> entities) const
	{
		for (const auto entity : entities) {
			if (auto existing = existingEntities.find(entity->hash); existing != existingEntities.end()) {
				entity->id = existing->second;
			}
		}
	}

	void
	Ingestor::storeNewEntities(DB::Connection * dbconn, const std::span<Entity *> entities) const
	{
		for (const auto entity : entities) {
			if (!entity->id) {
				storeNewEntity(dbconn, *entity);
				assert(entity->id);
			}
		}
	}

	void
	Ingestor::storeNewEntity(DB::Connection * dbconn, Entity & entity) const
	{
		static constexpr std::array<std::pair<std::string_view, void (Ingestor::*)(const Entity &) const>, 9>
				ENTITY_TYPE_VALUES {{
						{"host", nullptr},
						{"virtual_host", nullptr},
						{"path", nullptr},
						{"query_string", nullptr},
						{"referrer", nullptr},
						{"user_agent", &Ingestor::onNewUserAgent},
						{"unparsable_line", nullptr},
						{"uninsertable_line", nullptr},
						{"content_type", nullptr},
				}};

		assert(!entity.id);
		const auto & [typeName, onInsert] = ENTITY_TYPE_VALUES[std::to_underlying(entity.type)];
		bool entityNullDetail = true;
		std::tie(entity.id, entityNullDetail)
				= insert<EntityId, bool>(dbconn, SQL::ENTITY_INSERT, SQL::ENTITY_INSERT_OPTS, entity.value, typeName);
		if (onInsert && entityNullDetail) {
			std::invoke(onInsert, this, entity);
		}
		stats.entitiesInserted += 1;
	}

	void
	Ingestor::onNewUserAgent(const Entity & entity) const
	{
		const auto & [entityHash, entityId, type, value] = entity;
		auto curlOp = curlGetUserAgentDetail(*entityId, value, settings.userAgentAPI.c_str());
		{
			std::lock_guard curlOperationsLock {curlOperationsMutex};
			auto added = curlOperations.emplace(curlOp->hnd.get(), std::move(curlOp));
			curl_multi_add_handle(curl.get(), added.first->first);
		}
	}

	template<typename... T>
	void
	Ingestor::storeLogLine(DB::Connection * dbconn, const std::tuple<T...> & values) const
	{
		auto insert = dbconn->modify(SQL::ACCESS_LOG_INSERT, SQL::ACCESS_LOG_INSERT_OPTS);

		insert->bindParam(0, hostnameId);
		std::apply(
				[&insert](auto &&... value) {
					unsigned int param = 1;
					(insert->bindParam(param++, value), ...);
				},
				values);

		stats.logsInserted += insert->execute();
	}

	void
	Ingestor::logStats() const
	{
		log(LOG_INFO,
				"Statistics: linesQueued %zu, linesRead %zu, linesParsed %zu, linesParseFailed %zu, logsInserted %zu, "
				"entitiesInserted %zu, entitiesKnown %zu",
				queuedLines.size(), stats.linesRead, stats.linesParsed, stats.linesParseFailed, stats.logsInserted,
				stats.entitiesInserted, existingEntities.size());
	}

	void
	Ingestor::clearStats()
	{
		stats = {};
	}
}
