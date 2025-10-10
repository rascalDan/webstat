#include "ingestor.hpp"
#include "sql.hpp"
#include "uaLookup.hpp"
#include "util.hpp"
#include <connection.h>
#include <dbTypes.h>
#include <fstream>
#include <modifycommand.h>
#include <ranges>
#include <scn/scan.h>
#include <syslog.h>
#include <utility>
#include <zlib.h>

namespace DB {
	template<>
	void
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
			return static_cast<Crc32Value>(::crc32(::crc32(0, Z_NULL, 0), reinterpret_cast<const Bytef *>(value.data()),
					static_cast<uInt>(value.length())));
		}

		Entity
		toEntity(const std::string_view value, const EntityType type)
		{
			return {crc32(value), type, value};
		}

		std::optional<Entity>
		toEntityo(const std::optional<std::string_view> value, const EntityType type)
		{
			return value.transform([type](auto && value) {
				return toEntity(value, type);
			});
		}

		auto
		crc32ScanValues(const Ingestor::ScanValues & values)
		{
			return std::apply(
					[](auto &&... value) {
						return std::make_tuple(toEntity(value...[0], EntityType::VirtualHost), value...[1], value...[2],
								value...[3], toEntity(value...[4], EntityType::Path),
								toEntityo(value...[5], EntityType::QueryString), value...[6], value...[7], value...[8],
								value...[9], toEntityo(value...[10], EntityType::Referrer),
								toEntityo(value...[11], EntityType::UserAgent));
					},
					values);
		}
	}

	Ingestor::Ingestor(const utsname & host, IngestorSettings settings) :
		Ingestor {host,
				std::make_shared<DB::ConnectionPool>(
						settings.dbMax, settings.dbKeep, settings.dbType, settings.dbConnStr),
				std::move(settings)}
	{
	}

	Ingestor::Ingestor(const utsname & host, DB::ConnectionPoolPtr dbpl, IngestorSettings settings) :
		settings {std::move(settings)}, dbpool {std::move(dbpl)}, hostnameId {crc32(host.nodename)},
		curl {curl_multi_init()}
	{
		auto dbconn = dbpool->get();
		auto ins = dbconn->modify(SQL::HOST_UPSERT, SQL::HOST_UPSERT_OPTS);
		bindMany(ins, 0, hostnameId, host.nodename, host.sysname, host.release, host.version, host.machine,
				host.domainname);
		ins->execute();
	}

	Ingestor::ScanResult
	Ingestor::scanLogLine(std::string_view input)
	{
		return scn::scan< // Field : Apache format specifier : example
				std::string_view, // virtual_host : %v : some.host.name
				std::string_view, // remoteip : %a : 1.2.3.4 (or ipv6)
				uint64_t, // request_time : %{usec}t : 123456790
				std::string_view, // method : %m : GET
				QuotedString, // URL : "%u" : "/foo/bar"
				QueryString, // query_string : "%q" : "?query=string" or ""
				std::string_view, // protocol : %r : HTTPS/2.0
				unsigned short, // status : %>s : 200
				unsigned int, // size : %B : 1234
				unsigned int, // duration : %D : 1234
				CLFString, // referrer : "%{Referer}i" : "https://google.com/whatever" or "-"
				CLFString // user_agent : "%{User-agent}i" : "Chromium v123.4" or "-"
				>(input, R"({} {} {} {:[A-Z]} {} {} {} {} {} {} {} {})");
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

		for (int interesting = 0;
				curl_multi_poll(curl.get(), &logIn, 1, settings.idleJobsAfter, &interesting) == CURLM_OK;) {
			if (logIn.revents) {
				if (auto line = scn::scan<std::string>(input, "{:[^\n]}\n")) {
					linesRead++;
					ingestLogLine(line->value());
				}
				else {
					break;
				}
			}
			else if (!interesting) {
				runJobsIdle();
			}
			if (!curlOperations.empty()) {
				handleCurlOperations();
			}
		}
		while (!curlOperations.empty() && curl_multi_poll(curl.get(), nullptr, 0, INT_MAX, nullptr) == CURLM_OK) {
			handleCurlOperations();
		}
	}

	void
	Ingestor::ingestLogLine(const std::string_view line)
	{
		try {
			ingestLogLine(dbpool->get().get(), line);
		}
		catch (const std::exception &) {
			parkLogLine(line);
		}
	}

	void
	Ingestor::ingestLogLine(DB::Connection * dbconn, const std::string_view line)
	{
		auto rememberNewEntityIds = [this](const auto & ids) {
			existingEntities.insert_range(ids | std::views::take_while(&std::optional<Crc32Value>::has_value)
					| std::views::transform([](auto && value) {
						  return *value;
					  }));
		};
		if (auto result = scanLogLine(line)) {
			linesParsed++;
			const auto values = crc32ScanValues(result->values());
			NewEntityIds ids;
			{
				std::optional<DB::TransactionScope> dbtx;
				if (const auto newEnts = newEntities(values); newEnts.front()) {
					dbtx.emplace(*dbconn);
					ids = storeEntities(dbconn, newEnts);
				}
				storeLogLine(dbconn, values);
			}
			rememberNewEntityIds(ids);
		}
		else {
			linesDiscarded++;
			const auto unparsableLine = toEntity(line, EntityType::UnparsableLine);
			rememberNewEntityIds(storeEntities(dbconn, {unparsableLine}));
		}
	}

	void
	Ingestor::parkLogLine(std::string_view line)
	{
		std::ofstream {settings.fallbackDir / std::format("parked-{}.log", crc32(line))} << line;
		linesParked++;
	}

	void
	Ingestor::runJobsIdle()
	{
		const auto now = JobLastRunTime::clock::now();
		auto runJobAsNeeded = [this, now](auto job, JobLastRunTime & lastRun, auto freq) {
			try {
				if (lastRun + freq < now) {
					(this->*job)();
					lastRun = now;
				}
			}
			catch (const std::exception &) {
				// Error, retry in half the frequency
				lastRun = now - (freq / 2);
			}
		};
		runJobAsNeeded(&Ingestor::jobIngestParkedLines, lastRunIngestParkedLines, settings.freqIngestParkedLines);
	}

	void
	Ingestor::jobIngestParkedLines()
	{
		for (auto pathIter = std::filesystem::directory_iterator {settings.fallbackDir};
				pathIter != std::filesystem::directory_iterator {}; ++pathIter) {
			if (scn::scan<Crc32Value>(pathIter->path().filename().string(), "parked-{}.log")) {
				jobIngestParkedLine(pathIter);
			}
		}
	}

	void
	Ingestor::jobIngestParkedLine(const std::filesystem::directory_iterator & pathIter)
	{
		jobIngestParkedLine(pathIter->path(), pathIter->file_size());
	}

	void
	Ingestor::jobIngestParkedLine(const std::filesystem::path & path, uintmax_t size)
	{
		if (std::ifstream parked {path}) {
			std::string line;
			line.resize_and_overwrite(size, [&parked](char * content, size_t size) {
				parked.read(content, static_cast<std::streamsize>(size));
				return static_cast<size_t>(parked.tellg());
			});
			if (line.length() < size) {
				throw std::system_error {errno, std::generic_category(), "Short read of parked file"};
			}
			ingestLogLine(dbpool->get().get(), line);
		}
		else {
			throw std::system_error {errno, std::generic_category(), strerror(errno)};
		}
		std::filesystem::remove(path);
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
		static constexpr std::array ENTITY_TYPE_VALUES {
				"host", "virtual_host", "path", "query_string", "referrer", "user_agent", "unparsable_line"};

		auto insert = dbconn->modify(SQL::ENTITY_INSERT, SQL::ENTITY_INSERT_OPTS);
		NewEntityIds ids;
		std::ranges::transform(values | std::views::take_while(&std::optional<Entity>::has_value), ids.begin(),
				[this, &insert](auto && entity) {
					const auto & [entityId, type, value] = *entity;
					bindMany(insert, 0, entityId, ENTITY_TYPE_VALUES[std::to_underlying(type)], value);
					if (insert->execute() > 0) {
						switch (type) {
							case EntityType::UserAgent: {
								auto curlOp = curlGetUserAgentDetail(entityId, value, settings.userAgentAPI.c_str());
								auto added = curlOperations.emplace(curlOp->hnd.get(), std::move(curlOp));
								curl_multi_add_handle(curl.get(), added.first->first);
								break;
							}
							default:
								break;
						}
					}
					return std::get<0>(*entity);
				});
		return ids;
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
