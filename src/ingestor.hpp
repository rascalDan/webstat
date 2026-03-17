#pragma once

#include "curlOp.hpp"
#include "logTypes.hpp"
#include "settings.hpp"
#include <c++11Helpers.h>
#include <connectionPool.h>
#include <connection_fwd.h>
#include <cstdio>
#include <flat_set>
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
		Ingestor(const utsname &, IngestorSettings);
		Ingestor(const utsname &, DB::ConnectionPoolPtr, IngestorSettings);

		virtual ~Ingestor() = default;
		SPECIAL_MEMBERS_DELETE(Ingestor);

		using ScanResult = decltype(scn::scan<std::string_view, std::string_view, uint64_t, std::string_view,
				QuotedString, QueryString, std::string_view, unsigned short, unsigned int, unsigned int, CLFString,
				CLFString, CLFString>(std::declval<std::string_view>(), ""));
		using ScanValues = std::remove_cvref_t<decltype(std::declval<WebStat::Ingestor::ScanResult>()->values())>;

		[[nodiscard]] static ScanResult scanLogLine(std::string_view);

		void ingestLog(std::FILE *);
		void ingestLogLine(std::string_view);
		void ingestLogLine(DB::Connection *, std::string_view);
		void parkLogLine(std::string_view);
		void runJobsAsNeeded();

		unsigned int jobIngestParkedLines();
		unsigned int jobPurgeOldLogs();

		template<typename... T> void storeLogLine(DB::Connection *, const std::tuple<T...> &) const;

		IngestorSettings settings;

	protected:
		DB::ConnectionPoolPtr dbpool;

		size_t linesRead = 0;
		size_t linesParsed = 0;
		size_t linesDiscarded = 0;
		size_t linesParked = 0;
		mutable std::flat_set<Crc32Value> existingEntities;

		struct Job {
			using LastRunTime = std::chrono::system_clock::time_point;
			using Impl = unsigned int (Ingestor::*)();

			explicit Job(Impl impl) : impl(impl) { }

			const Impl impl;
			LastRunTime lastRun {LastRunTime::clock::now()};
			std::optional<std::thread> currentRun {std::nullopt};
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

		void jobIngestParkedLine(const std::filesystem::directory_iterator &);
		void jobIngestParkedLine(const std::filesystem::path &, uintmax_t size);

		using CurlOperations = std::map<CURL *, std::unique_ptr<CurlOperation>>;
		uint32_t hostnameId;
		CurlMultiPtr curl;
		mutable CurlOperations curlOperations;
		std::thread::id mainThread;
	};
}
