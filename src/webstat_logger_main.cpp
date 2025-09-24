#include "ingestor.hpp"
#include <boost/program_options.hpp>
#include <format>
#include <iostream>
#include <pq-connection.h>
#include <sys/utsname.h>

namespace {
	[[nodiscard]]
	utsname
	getHostDetail()
	{
		utsname uts {};
		if (uname(&uts)) {
			throw std::runtime_error(std::format("Failed to get hostname (uts: {}:{})", errno, strerror(errno)));
		}
		return uts;
	}
}

int
main(int argc, char ** argv)
{
	namespace po = boost::program_options;
	po::options_description opts("WebStat logger");

	WebStat::IngestorSettings settings;

	// clang-format off
	opts.add_options()
		("help,h", "Show this help message")
		("config,c", po::value<std::string>(), "Read config from this config file")
		("db.type", po::value(&settings.dbType)->default_value(settings.dbType),
		 "Database connection type")
		("db.wr.connstr,D", po::value(&settings.dbConnStr)->default_value(settings.dbConnStr),
		 "Database connection string (read/write)")
		("db.wr.max", po::value(&settings.dbMax)->default_value(settings.dbMax),
		 "Maximum number of concurrent write/read write DB connections")
		("db.wr.keep", po::value(&settings.dbKeep)->default_value(settings.dbKeep),
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

	try {
		WebStat::Ingestor {getHostDetail(), std::move(settings)}.ingestLog(stdin);
		return EXIT_SUCCESS;
	}
	catch (const std::exception & excp) {
		std::println(std::cerr, "Unhandled error: {}", excp.what());
		return EXIT_FAILURE;
	}
}
