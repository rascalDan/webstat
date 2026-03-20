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
#include <syslog.h>
#include <utility>
#include <zlib.h>

namespace DB {
	template<>
	void
	// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
	DB::Command::bindParam(unsigned int idx, const WebStat::Entity & entity)
	{
		bindParamI(idx, std::get<0>(entity));
	}
}

namespace WebStat {
	namespace {
		Crc32Value
		crc32(const std::string_view value)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - correct for crc32ing raw bytes
			return static_cast<Crc32Value>(::crc32(::crc32(0, Z_NULL, 0), reinterpret_cast<const Bytef *>(value.data()),
					static_cast<uInt>(value.length())));
		}

		template<EntityType Type> struct ToEntity {
			Entity
			operator()(const std::string_view value) const
			{
				return {crc32(value), Type, value};
			}

			template<typename T>
			std::optional<Entity>
			operator()(const std::optional<T> & value) const
			{
				return value.transform([this](auto && value) {
					return (*this)(value);
				});
			}
		};

		auto
		crc32ScanValues(const Ingestor::ScanValues & values)
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

	}

	Ingestor * Ingestor::currentIngestor = nullptr;

	Ingestor::Ingestor(const utsname & host, IngestorSettings settings) :
		Ingestor {host,
				std::make_shared<DB::ConnectionPool>(
						settings.dbMax, settings.dbKeep, settings.dbType, settings.dbConnStr),
				std::move(settings)}
	{
	}

	Ingestor::Ingestor(const utsname & host, DB::ConnectionPoolPtr dbpl, IngestorSettings settings) :
		settings {std::move(settings)}, dbpool {std::move(dbpl)}, ingestParkedLines {&Ingestor::jobIngestParkedLines},
		purgeOldLogs {&Ingestor::jobPurgeOldLogs}, hostnameId {crc32(host.nodename)}, curl {curl_multi_init()},
		mainThread {std::this_thread::get_id()}
	{
		auto dbconn = dbpool->get();
		auto ins = dbconn->modify(SQL::HOST_UPSERT, SQL::HOST_UPSERT_OPTS);
		bindMany(ins, 0, hostnameId, host.nodename, host.sysname, host.release, host.version, host.machine,
				host.domainname);
		ins->execute();

		assert(!currentIngestor);
		currentIngestor = this;
		signal(SIGTERM, &sigtermHandler);
		queuedLines.reserve(settings.maxBatchSize);
	}

	Ingestor::~Ingestor()
	{
		assert(currentIngestor);
		signal(SIGTERM, SIG_DFL);
		currentIngestor = nullptr;
	}

	void
	Ingestor::sigtermHandler(int sigNo)
	{
		assert(currentIngestor);
		currentIngestor->terminate(sigNo);
	}

	void
	Ingestor::terminate(int)
	{
		terminated = true;
		curl_multi_wakeup(curl.get());
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

	void
	Ingestor::handleCurlOperations()
	{
		int remaining {};
		curl_multi_perform(curl.get(), nullptr);
		while (auto msg = curl_multi_info_read(curl.get(), &remaining)) {
			if (msg->msg == CURLMSG_DONE) {
				if (auto operationItr = curlOperations.find(msg->easy_handle); operationItr != curlOperations.end()) {
					if (msg->data.result == CURLE_OK) {
						operationItr->second->whenComplete(dbpool->get().get());
					}
					else {
						operationItr->second->onError(dbpool->get().get());
					}
					curl_multi_remove_handle(curl.get(), msg->easy_handle);
					curlOperations.erase(operationItr);
				}
				else {
					curlOperations.erase(msg->easy_handle);
					std::println(std::cerr, "Failed to lookup CurlOperation");
				}
			}
		}
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
					linesRead++;
					queuedLines.emplace_back(std::move(line->value()));
					if (queuedLines.size() >= settings.maxBatchSize) {
						tryIngestQueuedLogLines();
					}
				}
				else {
					break;
				}
			}
			else {
				tryIngestQueuedLogLines();
			}
			if (expiredThenSet(lastCheckedJobs, settings.checkJobsAfter)) {
				runJobsAsNeeded();
			}
			if (!curlOperations.empty()) {
				handleCurlOperations();
			}
		}
		tryIngestQueuedLogLines();
		parkQueuedLogLines();
		while (!curlOperations.empty() && curl_multi_poll(curl.get(), nullptr, 0, INT_MAX, nullptr) == CURLM_OK) {
			handleCurlOperations();
		}
	}

	void
	Ingestor::tryIngestQueuedLogLines()
	{
		try {
			ingestLogLines(dbpool->get().get(), queuedLines);
			queuedLines.clear();
		}
		catch (const std::exception &) {
			existingEntities.clear();
		}
	}

	void
	Ingestor::ingestLogLines(DB::Connection * dbconn, const LineBatch & lines)
	{
		auto nonNullEntityIds = std::views::take_while(&std::optional<Crc32Value>::has_value)
				| std::views::transform([](auto && value) {
					  return *value;
				  });

		DB::TransactionScope batchTx {*dbconn};
		for (const auto & line : lines) {
			if (auto result = scanLogLine(line)) {
				linesParsed++;
				const auto values = crc32ScanValues(result->values());
				try {
					DB::TransactionScope dbtx {*dbconn};
					if (const auto newEnts = newEntities(values); newEnts.front()) {
						existingEntities.insert_range(storeEntities(dbconn, newEnts) | nonNullEntityIds);
					}
					storeLogLine(dbconn, values);
				}
				catch (const DB::Error & originalError) {
					try {
						DB::TransactionScope dbtx {*dbconn};
						const auto uninsertableLine = ToEntity<EntityType::UninsertableLine> {}(line);
						storeEntities(dbconn, {uninsertableLine});
					}
					catch (const std::exception &) {
						throw originalError;
					}
				}
			}
			else {
				linesDiscarded++;
				const auto unparsableLine = ToEntity<EntityType::UnparsableLine> {}(line);
				storeEntities(dbconn, {unparsableLine});
			}
		}
	}

	void
	Ingestor::parkQueuedLogLines()
	{
		if (queuedLines.empty()) {
			return;
		}
		std::string path {settings.fallbackDir / std::format("parked-{}.log", crc32(queuedLines.front()))};
		if (auto parked = FilePtr(fopen(path.c_str(), "w"))) {
			fprintf(parked.get(), "%zu\n", queuedLines.size());
			for (const auto & line : queuedLines) {
				fprintf(parked.get(), "%.*s\n", static_cast<int>(line.length()), line.data());
			}
			if (fflush(parked.get()) == 0) {
				linesParked += queuedLines.size();
				queuedLines.clear();
			}
			else {
				std::filesystem::remove(path);
			}
		}
	}

	void
	Ingestor::runJobsAsNeeded()
	{
		auto runJobAsNeeded = [this, now = Job::LastRunTime::clock::now()](Job & job, auto freq) {
			if (job.currentRun) {
				if (job.currentRun->valid()) {
					try {
						job.currentRun->get();
						job.lastRun = now;
					}
					catch (const std::exception &) {
						// Error, retry in half the frequency
						job.lastRun = now - (freq / 2);
					}
					job.currentRun.reset();
				}
			}
			else if (expired(job.lastRun, freq, now)) {
				job.currentRun.emplace(std::async(job.impl, this));
			}
		};
		runJobAsNeeded(ingestParkedLines, settings.freqIngestParkedLines);
		runJobAsNeeded(purgeOldLogs, settings.freqPurgeOldLogs);
	}

	unsigned int
	Ingestor::jobIngestParkedLines()
	{
		unsigned int count = 0;
		for (auto pathIter = std::filesystem::directory_iterator {settings.fallbackDir};
				pathIter != std::filesystem::directory_iterator {}; ++pathIter) {
			if (scn::scan<Crc32Value>(pathIter->path().filename().string(), "parked-{}.log")) {
				jobIngestParkedLines(pathIter->path());
				count += 1;
			}
		}
		return count;
	}

	void
	Ingestor::jobIngestParkedLines(const std::filesystem::path & path)
	{
		if (auto parked = FilePtr(fopen(path.c_str(), "r"))) {
			if (auto count = scn::scan<size_t>(parked.get(), "{}\n")) {
				jobIngestParkedLines(parked.get(), count->value());
				std::filesystem::remove(path);
				return;
			}
		}
		throw std::system_error {errno, std::generic_category(), strerror(errno)};
	}

	void
	Ingestor::jobIngestParkedLines(FILE * lines, size_t count)
	{
		for (size_t line = 0; line < count; ++line) {
			if (auto line = scn::scan<std::string>(lines, "{:[^\n]}\n")) {
				linesRead++;
				queuedLines.emplace_back(std::move(line->value()));
			}
			else {
				throw std::system_error {errno, std::generic_category(), "Short read of parked file"};
			}
		}
	}

	unsigned int
	Ingestor::jobPurgeOldLogs()
	{
		auto dbconn = dbpool->get();
		const auto stopAt = Job::LastRunTime::clock::now() + settings.purgeDeleteMaxTime;
		const auto purge = dbconn->modify(SQL::ACCESS_LOG_PURGE_OLD, SQL::ACCESS_LOG_PURGE_OLD_OPTS);
		purge->bindParam(0, settings.purgeDeleteMax);
		purge->bindParam(1, std::format("{} days", settings.purgeDaysToKeep));
		unsigned int purgedTotal {};
		while (stopAt > Job::LastRunTime::clock::now()) {
			const auto purged = purge->execute();
			purgedTotal += purged;
			if (purged < settings.purgeDeleteMax) {
				break;
			}
			std::this_thread::sleep_for(settings.purgeDeletePause);
		}
		return purgedTotal;
	}

	template<typename... T>
	Ingestor::NewEntities
	Ingestor::newEntities(const std::tuple<T...> & values) const
	{
		Ingestor::NewEntities rtn;
		auto next = rtn.begin();
		visit(
				[this, &next]<typename X>(const X & entity) {
					auto addNewIfReqd = [&next, this](auto && entity) mutable {
						if (!existingEntities.contains(std::get<0>(entity))) {
							*next++ = entity;
						}
						return 0;
					};

					if constexpr (std::is_same_v<X, Entity>) {
						addNewIfReqd(entity);
					}
					else if constexpr (std::is_same_v<X, std::optional<Entity>>) {
						entity.transform(addNewIfReqd);
					}
				},
				values);
		return rtn;
	}

	Ingestor::NewEntityIds
	Ingestor::storeEntities(DB::Connection * dbconn, const std::span<const std::optional<Entity>> values) const
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

		auto insert = dbconn->modify(SQL::ENTITY_INSERT, SQL::ENTITY_INSERT_OPTS);
		NewEntityIds ids;
		std::ranges::transform(values | std::views::take_while(&std::optional<Entity>::has_value), ids.begin(),
				[this, &insert](auto && entity) {
					const auto & [entityId, type, value] = *entity;
					const auto & [typeName, onInsert] = ENTITY_TYPE_VALUES[std::to_underlying(type)];
					bindMany(insert, 0, entityId, typeName, value);
					if (insert->execute() > 0 && onInsert && std::this_thread::get_id() == mainThread) {
						std::invoke(onInsert, this, *entity);
					}
					return std::get<0>(*entity);
				});
		return ids;
	}

	void
	Ingestor::onNewUserAgent(const Entity & entity) const
	{
		const auto & [entityId, type, value] = entity;
		auto curlOp = curlGetUserAgentDetail(entityId, value, settings.userAgentAPI.c_str());
		auto added = curlOperations.emplace(curlOp->hnd.get(), std::move(curlOp));
		curl_multi_add_handle(curl.get(), added.first->first);
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

		insert->execute();
	}
}
