#include <benchmark/benchmark.h>
#include <filesystem>

#include "testing-util.hpp"
#include <c++11Helpers.h>

#include <ingestor.hpp>

namespace {
	const std::filesystem::path TMP_LOG = std::format("/tmp/webstat-perf-{}.log", getpid());

	constexpr size_t LOG_LINES = 10000;
	const WebStat::LogFile LOG_FILE {TMP_LOG, LOG_LINES};

	void
	setup(const benchmark::State &)
	{
		static const WebStat::MockDB mockdb;
	}

	void
	doIngestFile(benchmark::State & state)
	{
		WebStat::Ingestor ingestor {WebStat::getTestUtsName("perf-hostname"),
				std::make_shared<WebStat::MockDBPool>("webstat"),
				{
						.userAgentAPI = {},
						.maxBatchSize = static_cast<size_t>(state.range(0)),
				}};
		for (auto loop : state) {
			WebStat::FilePtr logFile {fopen(TMP_LOG.c_str(), "r")};
			ingestor.ingestLog(logFile.get());
		}
	}
}

BENCHMARK_RANGE(doIngestFile, 1, 1024)->Setup(setup);

BENCHMARK_MAIN();
