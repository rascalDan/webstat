#include "ingestor.hpp"
#include <boost/program_options.hpp>
#include <format>
#include <iostream>
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
main(int argc, char ** argv)
{
	namespace po = boost::program_options;
	po::options_description opts("WebStat logger");

	// clang-format off
	opts.add_options()
		("help,h", "Show this help message")
		("config,c", po::value<std::string>(), "Read config from this config file")
		;
	// clang-format on
	po::variables_map optVars;
	po::store(po::command_line_parser(argc, argv).options(opts).run(), optVars);
	if (const auto & rcPath = optVars.find("config"); rcPath != optVars.end()) {
		po::store(po::parse_config_file(rcPath->second.as<std::string>().c_str(), opts), optVars);
	}

	// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
	if (optVars.contains("help")) {
		std::cout << opts << '\n';
		return EXIT_FAILURE;
	}
	po::notify(optVars);

	auto pool = std::make_shared<DB::ConnectionPool>(1, 1, "postgresql", "dbname=webstat user=webstat");
	WebStat::Ingestor {getHostname(false), pool}.ingestLog(stdin);
	return EXIT_SUCCESS;
}
