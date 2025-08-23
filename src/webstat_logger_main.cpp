#include "ingestor.hpp"
#include <format>
#include <pq-connection.h>
#include <sys/utsname.h>

namespace {
	[[nodiscard]]
	std::string
	getHostname(bool fqdn)
	{
		utsname uts {};
		if (uname(&uts)) {
			throw std::runtime_error(std::format("Failed to get hostname (uts: {}:{})", errno, strerror(errno)));
		}
		if (fqdn) {
			return std::format("{}.{}", uts.nodename, uts.domainname);
		}
		return uts.nodename;
	}
}

int
main(int, char **)
{
	auto dbconn = std::make_shared<PQ::Connection>("dbname=webstat user=webstat");
	WebStat::Ingestor {getHostname(false), dbconn}.ingestLog(stdin);
}
