#pragma once

#include "curlOp.hpp"
#include "logTypes.hpp"
#include "settings.hpp"
#include <c++11Helpers.h>
#include <connectionPool.h>
#include <connection_fwd.h>
#include <cstdio>
#include <flat_set>
#include <future>
#include <scn/scan.h>
#include <span>
#include <sys/utsname.h>

namespace WebStat {
	using namespace std::chrono;
	using namespace std::chrono_literals;

	struct IngestorSettings : Settings {
		// NOLINTBEGIN(readability-magic-numbers)
		std::string dbConnStr = "dbname=webstat user=webstat";
		std::string userAgentAPI = "https://useragentstring.com";
		std::filesystem::path fallbackDir = "/var/log/webstat";
		unsigned int dbMax = 4;
		unsigned int dbKeep = 2;
		size_t maxBatchSize = 1;
		minutes checkJobsAfter = 1min;
		minutes freqIngestParkedLines = 30min;
		minutes freqPurgeOldLogs = 6h;
		unsigned int purgeDaysToKeep = 61; // ~2 months
		unsigned int purgeDeleteMax = 10'000;
		minutes purgeDeleteMaxTime = 5min;
		seconds purgeDeletePause = 3s;
		// NOLINTEND(readability-magic-numbers)
	};

	class Ingestor {
	public:
		using LineBatch = std::vector<std::string>;
		Ingestor(const utsname &, IngestorSettings);
		Ingestor(const utsname &, DB::ConnectionPoolPtr, IngestorSettings);

		virtual ~Ingestor();
		SPECIAL_MEMBERS_DELETE(Ingestor);

		using ScanResult = decltype(scn::scan<std::string_view, std::string_view, uint64_t, std::string_view,
				QuotedString, QueryString, std::string_view, unsigned short, unsigned int, unsigned int, CLFString,
				CLFString, CLFString>(std::declval<std::string_view>(), ""));
		using ScanValues = std::remove_cvref_t<decltype(std::declval<WebStat::Ingestor::ScanResult>()->values())>;

		[[nodiscard]] static ScanResult scanLogLine(std::string_view);

		void ingestLog(std::FILE *);
		void tryIngestQueuedLogLines();
		void ingestLogLines(DB::Connection *, const LineBatch & lines);
		void parkQueuedLogLines();
		void runJobsAsNeeded();

		unsigned int jobIngestParkedLines();
		unsigned int jobPurgeOldLogs();

		template<typename... T> void storeLogLine(DB::Connection *, const std::tuple<T...> &) const;

		IngestorSettings settings;

	protected:
		static Ingestor * currentIngestor;
		DB::ConnectionPoolPtr dbpool;

		size_t linesRead = 0;
		size_t linesParsed = 0;
		size_t linesDiscarded = 0;
		size_t linesParked = 0;
		std::flat_set<Crc32Value> existingEntities;
		LineBatch queuedLines;

		bool terminated = false;

		struct Job {
			using LastRunTime = std::chrono::system_clock::time_point;
			using Impl = unsigned int (Ingestor::*)();

			explicit Job(Impl jobImpl) : impl(jobImpl) { }

			const Impl impl;
			LastRunTime lastRun {LastRunTime::clock::now()};
			std::optional<std::future<unsigned int>> currentRun;
		};

		Job::LastRunTime lastCheckedJobs {Job::LastRunTime::clock::now()};
		Job ingestParkedLines;
		Job purgeOldLogs;

	private:
		static constexpr size_t MAX_NEW_ENTITIES = 6;
		using NewEntityIds = std::array<std::optional<Crc32Value>, MAX_NEW_ENTITIES>;
		NewEntityIds storeEntities(DB::Connection *, std::span<const std::optional<Entity>>) const;
		using NewEntities = std::array<std::optional<Entity>, MAX_NEW_ENTITIES>;
		template<typename... T> NewEntities newEntities(const std::tuple<T...> &) const;
		void onNewUserAgent(const Entity &) const;
		void handleCurlOperations();

		void jobIngestParkedLines(const std::filesystem::path &);
		void jobIngestParkedLines(FILE *, size_t count);

		static void sigtermHandler(int);
		void terminate(int);
		[[gnu::format(printf, 3, 4)]] virtual void log(int level, const char * msgfmt, ...) const = 0;

		using CurlOperations = std::map<CURL *, std::unique_ptr<CurlOperation>>;
		uint32_t hostnameId;
		CurlMultiPtr curl;
		mutable CurlOperations curlOperations;
		std::thread::id mainThread;
	};
}
