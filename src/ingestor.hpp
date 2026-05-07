#pragma once

#include "curlOp.hpp"
#include "logTypes.hpp"
#include "settings.hpp"
#include <c++11Helpers.h>
#include <connectionPool.h>
#include <connection_fwd.h>
#include <cstdio>
#include <expected>
#include <future>
#include <map>
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
		size_t maxBatches = 5;
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
		using LinesView = std::span<const std::string>;

		struct Job {
			using LastRunTime = std::chrono::system_clock::time_point;
			using Result = std::function<unsigned int()>;
			using Impl = Result (Ingestor::*)();
			using Cond = bool (Ingestor::*)();

			explicit Job(Impl jobImpl, Cond jobCond = nullptr) : impl {jobImpl}, cond {jobCond} { }

			const Impl impl;
			const Cond cond;
			LastRunTime lastRun {LastRunTime::clock::now()};
			std::optional<std::future<Result>> currentRun;
		};

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
		std::pair<std::future<Job::Result> &, bool> beginIngestQueuedLogLines();
		void ingestLogLines(DB::Connection *, LinesView lines);
		std::expected<std::filesystem::path, int> parkLogLines(LineBatch &);
		void runJobsAsNeeded();

		Job::Result jobReadParkedLines();
		Job::Result jobPurgeOldLogs();
		Job::Result jobStoreQueuedLines();

		template<typename... T> void storeLogLine(DB::Connection *, const std::tuple<T...> &) const;

		IngestorSettings settings;

		struct Stats {
			size_t linesRead;
			size_t linesParsed;
			size_t linesParseFailed;
			size_t logsInserted;
			size_t entitiesInserted;
			constexpr bool operator==(const Ingestor::Stats &) const = default;
		};

	protected:
		void finishAllJobs();

		static Ingestor * currentIngestor;
		DB::ConnectionPoolPtr dbpool;
		mutable Stats stats {};

		std::map<EntityHash, EntityId> existingEntities;
		LineBatch queuedLines, processingLines;

		bool terminated = false;

		Job::LastRunTime lastCheckedJobs {Job::LastRunTime::clock::now()};
		Job ingestParkedLines;
		Job purgeOldLogs;
		Job storeQueueLines;

	private:
		template<typename... T> static std::vector<Entity *> entities(std::tuple<T...> &);
		void fillKnownEntities(std::span<Entity *>) const;
		void storeNewEntities(DB::Connection *, std::span<Entity *>) const;
		void storeNewEntity(DB::Connection *, Entity &) const;
		void onNewUserAgent(const Entity &) const;
		void handleCurlOperations();
		void logStats() const;
		void clearStats();
		void finalizeJob(Job &, minutes freq, Job::LastRunTime::clock::time_point now);

		LineBatch jobReadParkedLines(const std::filesystem::path &);
		LineBatch jobReadParkedLines(FILE *, size_t count);

		static void sigtermHandler(int);
		void terminate(int);
		static void sigusr1Handler(int);
		static void sigusr2Handler(int);
		[[gnu::format(printf, 3, 4)]] virtual void log(int level, const char * msgfmt, ...) const = 0;

		using CurlOperations = std::map<CURL *, std::unique_ptr<CurlOperation>>;
		EntityId hostnameId;
		CurlMultiPtr curl;
		mutable CurlOperations curlOperations;
		mutable std::mutex curlOperationsMutex;
	};
}
