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

	std::string dbType;
	std::string dbConnStr;
	unsigned int dbMax = 4;
	unsigned int dbKeep = 2;

	// clang-format off
	opts.add_options()
		("help,h", "Show this help message")
		("config,c", po::value<std::string>(), "Read config from this config file")
		("db.type", po::value(&dbType)->default_value("postgresql"),
		 "Database connection type")
		("db.wr.connstr,D", po::value(&dbConnStr)->default_value("dbname=webstat user=webstat"),
		 "Database connection string (read/write)")
		("db.wr.max", po::value(&dbMax)->default_value(4),
		 "Maximum number of concurrent write/read write DB connections")
		("db.wr.keep", po::value(&dbKeep)->default_value(2),
		 "Number of write/read write DB connections to keep open")
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

	auto pool = std::make_shared<DB::ConnectionPool>(dbMax, dbKeep, std::move(dbType), std::move(dbConnStr));
	WebStat::Ingestor {getHostname(false), pool}.ingestLog(stdin);
	return EXIT_SUCCESS;
}
