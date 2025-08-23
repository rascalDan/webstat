#pragma once

#include "logTypes.hpp"
#include <c++11Helpers.h>
#include <connection_fwd.h>
#include <cstdio>
#include <scn/scan.h>

namespace WebStat {
	class Ingestor {
	public:
		Ingestor(DB::ConnectionPtr dbconn);

		virtual ~Ingestor() = default;
		SPECIAL_MEMBERS_DEFAULT_MOVE_NO_COPY(Ingestor);

		using ScanResult = decltype(scn::scan<std::string_view, std::string_view, uint64_t, std::string_view,
				QuotedString, QueryString, std::string_view, unsigned short, unsigned int, unsigned int, CLFString,
				CLFString>(std::declval<std::string_view>(), ""));
		using ScanValues = std::remove_cvref_t<decltype(std::declval<WebStat::Ingestor::ScanResult>()->values())>;

		[[nodiscard]] static ScanResult scanLogLine(std::string_view);

		void ingestLog(std::FILE *);

		template<typename T> void storeEntity(const T &) const;
		void storeEntity(Entity) const;
		void storeEntity(std::optional<Entity>) const;

	protected:
		size_t linesRead = 0;
		size_t linesParsed = 0;
		size_t linesDiscarded = 0;

	private:
		DB::ConnectionPtr dbconn;
	};
}
