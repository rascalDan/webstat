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
#undef XSTR
#undef STR

	using FilePtr = std::unique_ptr<std::FILE, DeleteWith<&fclose>>;

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
}
