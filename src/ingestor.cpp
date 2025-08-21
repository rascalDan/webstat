#include "ingestor.hpp"
#include <scn/scan.h>
#include <syslog.h>

namespace WebStat {
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
			if (auto result = scanLogLine(line->value())) {
				linesParsed++;
				std::ignore = result->values();
			}
			else {
				syslog(LOG_WARNING, "Discarded line: [%s]", line->value().c_str());
				linesDiscarded++;
			}
		}
	}
}
