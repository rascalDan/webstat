#pragma once

#include "logTypes.hpp"
#include <c++11Helpers.h>
#include <connection_fwd.h>
#include <cstdio>
#include <flat_set>
#include <scn/scan.h>
#include <span>

namespace WebStat {
	class Ingestor {
	public:
		Ingestor(std::string_view hostname, DB::ConnectionPtr dbconn);

		virtual ~Ingestor() = default;
		SPECIAL_MEMBERS_DEFAULT_MOVE_NO_COPY(Ingestor);

		using ScanResult = decltype(scn::scan<std::string_view, std::string_view, uint64_t, std::string_view,
				QuotedString, QueryString, std::string_view, unsigned short, unsigned int, unsigned int, CLFString,
				CLFString>(std::declval<std::string_view>(), ""));
		using ScanValues = std::remove_cvref_t<decltype(std::declval<WebStat::Ingestor::ScanResult>()->values())>;

		[[nodiscard]] static ScanResult scanLogLine(std::string_view);

		void ingestLog(std::FILE *);
		void ingestLogLine(std::string_view);

		template<typename... T> void storeLogLine(const std::tuple<T...> &) const;

	protected:
		size_t linesRead = 0;
		size_t linesParsed = 0;
		size_t linesDiscarded = 0;

	private:
		static constexpr size_t MAX_NEW_ENTITIES = 6;
		void storeEntities(std::span<const std::optional<Entity>>) const;
		using NewEntities = std::array<std::optional<Entity>, MAX_NEW_ENTITIES>;
		template<typename... T> NewEntities newEntities(const std::tuple<T...> &) const;

		mutable std::flat_set<Crc32Value> existingEntities;
		uint32_t hostnameId;
		DB::ConnectionPtr dbconn;
	};
}
