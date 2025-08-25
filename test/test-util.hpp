#pragma once

#include "pq-mock.h"
#include <filesystem>

namespace WebStat {
#define XSTR(s) STR(s)
#define STR(s) #s
	inline const std::filesystem::path SRC_DIR(XSTR(SRC));
#undef XSTR
#undef STR

	template<auto Deleter>
	using DeleteWith = decltype([](auto obj) {
		return Deleter(obj);
	});
	using FilePtr = std::unique_ptr<std::FILE, DeleteWith<&fclose>>;

	struct MockDB : public DB::PluginMock<PQ::Mock> {
		MockDB();
	};

	template<typename Out> using ParseData = std::tuple<std::string_view, Out>;
}
