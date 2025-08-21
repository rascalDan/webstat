#pragma once

#include "logTypes.hpp"
#include <cstdio>
#include <scn/scan.h>

namespace WebStat {
	class Ingestor {
	public:
		using ScanResult = decltype(scn::scan<std::string_view, std::string_view, uint64_t, std::string_view,
				QuotedString, QueryString, std::string_view, unsigned short, unsigned int, unsigned int, CLFString,
				CLFString>(std::declval<std::string_view>(), ""));

		[[nodiscard]] static ScanResult scanLogLine(std::string_view);

		void ingestLog(std::FILE *);

	protected:
		size_t linesRead = 0;
		size_t linesParsed = 0;
		size_t linesDiscarded = 0;
	};
}
