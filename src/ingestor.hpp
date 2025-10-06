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
		std::string dbConnStr = "dbname=webstat user=webstat";
		std::string userAgentAPI = "https://useragentstring.com";
		std::filesystem::path fallbackDir = "/var/log/webstat";
		unsigned int dbMax = 4;
		unsigned int dbKeep = 2;
		int idleJobsAfter = duration_cast<milliseconds>(1min).count();
		minutes freqIngestParkedLines = 30min;
	};

	class Ingestor {
	public:
		Ingestor(const utsname &, IngestorSettings);
		Ingestor(const utsname &, DB::ConnectionPoolPtr, IngestorSettings);

		virtual ~Ingestor() = default;
		SPECIAL_MEMBERS_DELETE(Ingestor);

		using ScanResult = decltype(scn::scan<std::string_view, std::string_view, uint64_t, std::string_view,
				QuotedString, QueryString, std::string_view, unsigned short, unsigned int, unsigned int, CLFString,
				CLFString>(std::declval<std::string_view>(), ""));
		using ScanValues = std::remove_cvref_t<decltype(std::declval<WebStat::Ingestor::ScanResult>()->values())>;

		[[nodiscard]] static ScanResult scanLogLine(std::string_view);

		void ingestLog(std::FILE *);
		void ingestLogLine(std::string_view);
		void ingestLogLine(DB::Connection *, std::string_view);
		void parkLogLine(std::string_view);
		void runJobsIdle();

		void jobIngestParkedLines();

		template<typename... T> void storeLogLine(DB::Connection *, const std::tuple<T...> &) const;

		IngestorSettings settings;

	protected:
		DB::ConnectionPoolPtr dbpool;

		size_t linesRead = 0;
		size_t linesParsed = 0;
		size_t linesDiscarded = 0;
		size_t linesParked = 0;

		using JobLastRunTime = std::chrono::system_clock::time_point;
		JobLastRunTime lastRunIngestParkedLines;

	private:
		static constexpr size_t MAX_NEW_ENTITIES = 6;
		void storeEntities(DB::Connection *, std::span<const std::optional<Entity>>) const;
		using NewEntities = std::array<std::optional<Entity>, MAX_NEW_ENTITIES>;
		template<typename... T> NewEntities newEntities(const std::tuple<T...> &) const;
		void handleCurlOperations();

		void jobIngestParkedLine(const std::filesystem::directory_iterator &);
		void jobIngestParkedLine(const std::filesystem::path &, uintmax_t size);

		using CurlOperations = std::map<CURL *, std::unique_ptr<CurlOperation>>;
		mutable std::flat_set<Crc32Value> existingEntities;
		uint32_t hostnameId;
		CurlMultiPtr curl;
		mutable CurlOperations curlOperations;
	};
}
