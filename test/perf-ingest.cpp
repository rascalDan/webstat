#include <benchmark/benchmark.h>
#include <filesystem>
#include <fstream>

#include "testing-util.hpp"

namespace {
	// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
	const std::filesystem::path TMP_LOG = std::format("/tmp/webstat-perf-{}.log", getpid());

	constexpr size_t LOG_LINES = 10000;
	// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
	const WebStat::LogFile LOG_FILE {TMP_LOG, LOG_LINES};

	void
	setup(const benchmark::State &)
	{
		static const WebStat::MockDB mockdb;
	}

	class PerfIngestor : public WebStat::BasicTestIngestor {
		using BasicTestIngestor::BasicTestIngestor;

	protected:
		[[gnu::format(printf, 3, 4)]] void
		// NOLINTNEXTLINE(modernize-avoid-variadic-functions)
		log(int, const char * msgfmt, ...) const override
		{
			va_list args;
			va_start(args, msgfmt);
			vfprintf(stderr, msgfmt, args);
			fputc('\n', stderr);
			va_end(args);
		}
	};

	void
	doIngestFile(benchmark::State & state)
	{
		PerfIngestor ingestor {"perf-hostname",
				{
						.userAgentAPI = {},
						.fallbackDir = std::format("/tmp/webstat-{}", getpid()),
						.maxBatchSize = static_cast<size_t>(state.range(0)),
				}};
		// NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
		for (const auto _ : state) {
			std::ifstream logFile {TMP_LOG};
			ingestor.ingestLog(logFile);
		}
	}
}

BENCHMARK_RANGE(doIngestFile, 1, 1024)->Setup(setup);

BENCHMARK_MAIN();
