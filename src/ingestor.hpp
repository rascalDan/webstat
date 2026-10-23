#pragma once

#include "curlOp.hpp"
#include "logTypes.hpp"
#include "settings.hpp"
#include <c++11Helpers.h>
#include <connectionPool.h>
#include <connection_fwd.h>
#include <expected>
#include <future>
#include <map>
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
		minutes maxBatchAge = 2min;
		minutes checkJobsAfter = 1min;
		minutes freqIngestParkedLines = 30min;
		minutes freqRetryUninsertableLines = 4h;
		minutes freqPurgeOldLogs = 6h;
		unsigned int purgeDaysToKeep = 61; // ~2 months
		unsigned int purgeDeleteMax = 10'000;
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

		Ingestor(IngestorSettings);
		Ingestor(DB::ConnectionPoolPtr, IngestorSettings);

		virtual ~Ingestor();
		SPECIAL_MEMBERS_DELETE(Ingestor);

		using ScanValues = std::tuple< // Field : Apache format specifier : example
				std::string_view, // virtual_host : %V : some.host.name
				std::string_view, // remoteip : %a : 1.2.3.4 (or ipv6)
				uint64_t, // request_time : %{usec}t : 123456790
				std::string_view, // method : %m : GET
				QuotedString, // path : "%u" : "/foo/bar"
				QueryString, // query_string : "%q" : "?query=string" or ""
				std::string_view, // protocol : %r : HTTPS/2.0
				uint16_t, // status : %>s : 200
				uint64_t, // size (bytes) : %B : 1234
				uint64_t, // duration (µs) : %D : 1234
				CLFString, // referrer : "%{Referer}i" : "https://google.com/whatever" or "-"
				CLFString, // user_agent : "%{User-agent}i" : "Chromium v123.4" or "-"
				CLFString // content_type : "%{Content-type}o" : "test/plain" or "-"
				>;
		using ScanResult = std::expected<ScanValues, std::invalid_argument>;
		using ReferrerUri = std::tuple<std::string, // scheme
				Entity, // host
				Entity, // path
				std::optional<Entity> // querystring
				>;

		[[nodiscard]] static ScanResult scanLogLine(std::string_view);

		void ingestLog(std::istream &);
		bool inputEvent(std::istream &);
		void finishCurlOperations();
		std::pair<std::future<Job::Result> &, bool> beginIngestQueuedLogLines();
		void ingestLogLines(DB::Connection *, LinesView lines);
		bool previousIngestErrored = false;
		std::expected<std::filesystem::path, int> parkLogLines(LineBatch &);
		void runJobsAsNeeded();

		Job::Result jobHandleCompleteCurlOps();
		Job::Result jobReadParkedLines();
		Job::Result jobPurgeOldLogs();
		Job::Result jobStoreQueuedLines();
		Job::Result jobRetryUninsertableLines();

		template<typename... T> void storeLogLine(DB::ModifyCommand * insert, const std::tuple<T...> &) const;

		IngestorSettings settings;

		struct Stats {
			size_t linesRead;
			size_t linesParsed;
			size_t linesParseFailed;
			size_t logsInserted;
			size_t linesParked;
			size_t entitiesInserted;
			size_t batchesStarted;
			size_t batchesCompleted;
			constexpr bool operator==(const Ingestor::Stats &) const = default;
		};

		static std::optional<ReferrerUri> decomposeReferrer(std::string_view);

	protected:
		void finishAllJobs();
		void runJobAsNeeded(Job &, std::chrono::minutes freq);

		static Ingestor * currentIngestor;
		DB::ConnectionPoolPtr dbpool;
		mutable Stats stats {};

		ThreadSafeT<std::map<EntityKey, EntityId>> existingEntities;
		std::optional<std::chrono::system_clock::time_point> forceBatchAfter;
		LineBatch queuedLines, processingLines;

		bool terminated = false;

		Job::LastRunTime lastCheckedJobs {Job::LastRunTime::clock::now()};
		Job handleCompleteCurlOps;
		Job ingestParkedLines;
		Job purgeOldLogs;
		Job storeQueueLines;
		Job retryUninsertableLines;

		template<typename... T> static std::vector<Entity *> entities(std::tuple<T...> &);
		void fillKnownEntities(std::span<Entity *>) const;
		void storeNewEntities(DB::Connection *, std::span<Entity *>) const;
		void storeNewEntity(DB::Connection *, Entity &) const;
		using EntityInsertResult = std::tuple<EntityId, bool>;
		EntityInsertResult insertGenericEntity(DB::Connection *, Entity &, std::string_view) const;
		EntityInsertResult insertReferrer(DB::Connection *, Entity &, std::string_view) const;
		static EntityId storeUnparsableLine(DB::Connection *, EntityId, std::string_view);
		static EntityId storeUninsertableLine(DB::Connection *, EntityId, std::string_view, const std::exception &);
		void onNewUserAgent(const Entity &) const;
		auto withCurlLock(auto &&...);
		bool haveCurlOperations();
		void handleCurlOperations();
		void logStats() const;
		void clearStats();
		void finalizeJob(Job &, minutes freq, Job::LastRunTime::clock::time_point now);

		LineBatch jobReadParkedLines(const std::filesystem::path &);
		LineBatch jobReadParkedLines(std::istream &, size_t count);

		static void sigtermHandler(int);
		void terminate(int);
		static void sigusr1Handler(int);
		static void sigusr2Handler(int);
		[[gnu::format(printf, 3, 4)]] virtual void log(int level, const char * msgfmt, ...) const = 0;
		virtual utsname getHostDetail() const = 0;
		EntityId getHostnameId(DB::Connection *) const;

		using CurlOperations = std::map<CURL *, std::unique_ptr<CurlOperation>>;
		CurlMultiPtr curl;
		mutable CurlOperations curlOperations;
		mutable std::mutex curlOperationsMutex;
	};
}
