#include "ingestor.hpp"
#include "sql.hpp"
#include "util.hpp"
#include <connection.h>
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
	DB::Command::bindParam(unsigned int idx, const WebStat::Entity & entity)
	{
		bindParamI(idx, entity.first);
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
		addCrc32(const std::string_view value)
		{
			return {crc32(value), value};
		}

		std::optional<Entity>
		addCrc32o(const std::optional<std::string_view> value)
		{
			return value.transform(addCrc32);
		}

		auto
		crc32ScanValues(const Ingestor::ScanValues & values)
		{
			return std::apply(
					[](auto &&... value) {
						return std::make_tuple(addCrc32(value...[0]), value...[1], value...[2], value...[3],
								addCrc32(value...[4]), addCrc32o(value...[5]), value...[6], value...[7], value...[8],
								value...[9], addCrc32o(value...[10]), addCrc32o(value...[11]));
					},
					values);
		}
	}

	Ingestor::Ingestor(const std::string_view hostname, DB::ConnectionPtr dbconn) :
		hostnameId {crc32(hostname)}, dbconn {std::move(dbconn)}
	{
		storeEntities({
				std::make_pair(hostnameId, hostname),
		});
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
	Ingestor::ingestLog(std::FILE * input)
	{
		while (auto line = scn::scan<std::string>(input, "{:[^\n]}\n")) {
			linesRead++;
			ingestLogLine(line->value());
		}
	}

	void
	Ingestor::ingestLogLine(const std::string_view line)
	{
		if (auto result = scanLogLine(line)) {
			linesParsed++;
			const auto values = crc32ScanValues(result->values());
			std::optional<DB::TransactionScope> dbtx;
			if (const auto newEnts = newEntities(values); newEnts.front()) {
				dbtx.emplace(*dbconn);
				storeEntities(newEnts);
			}
			storeLogLine(values);
		}
		else {
			syslog(LOG_WARNING, "Discarded line: [%.*s]", static_cast<int>(line.length()), line.data());
			linesDiscarded++;
		}
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
						if (!existingEntities.contains(entity.first)) {
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

	void
	Ingestor::storeEntities(const std::span<const std::optional<Entity>> values) const
	{
		std::ranges::for_each(
				values | std::views::take_while(&std::optional<Entity>::has_value), [this](auto && entity) {
					auto insert = dbconn->modify(SQL::ENTITY_INSERT, SQL::ENTITY_INSERT_OPTS);
					insert->bindParamI(0, entity->first);
					insert->bindParamS(1, entity->second);
					insert->execute();
					existingEntities.emplace(entity->first);
				});
	}

	template<typename... T>
	void
	Ingestor::storeLogLine(const std::tuple<T...> & values) const
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
