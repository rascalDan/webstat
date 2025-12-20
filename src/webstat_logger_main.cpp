#include "ingestor.hpp"
#include "util.hpp"
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

#define LEXICAL_CAST_DURATION(UNIT) \
	template<> std::chrono::UNIT boost::lexical_cast<std::chrono::UNIT, std::string>(const std::string & input) \
	{ \
		return WebStat::parseDuration<std::chrono::UNIT::rep, std::chrono::UNIT::period>(input); \
	}

LEXICAL_CAST_DURATION(milliseconds);
LEXICAL_CAST_DURATION(seconds);
LEXICAL_CAST_DURATION(minutes);
LEXICAL_CAST_DURATION(hours);
LEXICAL_CAST_DURATION(days);
LEXICAL_CAST_DURATION(weeks);
#undef LEXICAL_CAST_DURATION

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
		("fallback.dir", po::value(&settings.fallbackDir)->default_value(settings.fallbackDir),
		 "Path to write access logs to when the database is unavailable")
		("jobs.idle", po::value(&settings.idleJobsAfter)->default_value(settings.idleJobsAfter),
		 "Run idle when there's no activity for this period (ms)")
		("job.parked.freq", po::value(&settings.freqIngestParkedLines)->default_value(settings.freqIngestParkedLines),
		 "How often to check for and import parked log lines")
		("job.purge.freq", po::value(&settings.freqPurgeOldLogs)->default_value(settings.freqPurgeOldLogs),
		 "How often to purge old access log entries from the database")
		("job.purge.days", po::value(&settings.purgeDaysToKeep)->default_value(settings.purgeDaysToKeep),
		 "How many days of access log entries to keep")
		("job.purge.max", po::value(&settings.purgeDeleteMax)->default_value(settings.purgeDeleteMax),
		 "Maximum number of access log entries to delete in a single operation")
		("job.purge.time", po::value(&settings.purgeDeleteMaxTime)->default_value(settings.purgeDeleteMaxTime),
		 "Maximum amount of time to spending purging old access log entries before continuing to ingest")
		("job.purge.pause", po::value(&settings.purgeDeletePause)->default_value(settings.purgeDeletePause),
		 "Time to pause for between repeated exections of a delete operation")
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
