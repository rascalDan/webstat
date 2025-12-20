#pragma once

#include <connectionPool.h>
#include <filesystem>
#include <pq-mock.h>
#include <sys/utsname.h>
#include <util.hpp>

namespace WebStat {
#define XSTR(s) STR(s)
#define STR(s) #s
	inline const std::filesystem::path SRC_DIR(XSTR(SRC));
	inline const std::filesystem::path TEST_DIR(XSTR(TEST));
	inline const std::filesystem::path FIXTURE_DIR(XSTR(FIXTURES));
	inline const std::string FIXTURE_URL_BASE = "file://" + std::filesystem::canonical(FIXTURE_DIR).string();
#undef XSTR
#undef STR

	struct MockDB : public DB::PluginMock<PQ::Mock> {
		MockDB();
	};

	class MockDBPool : public DB::BasicConnectionPool {
	public:
		MockDBPool(std::string);

	protected:
		DB::ConnectionPtr createResource() const;

	private:
		std::string name;
	};

	template<typename Out> using ParseData = std::tuple<std::string_view, Out>;

	utsname getTestUtsName(std::string_view);

	struct LogFile {
		LogFile(std::filesystem::path where, size_t entries);
		~LogFile();
		SPECIAL_MEMBERS_DELETE(LogFile);

		const std::filesystem::path path;
	};
}
