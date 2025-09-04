#pragma once

#include <command.h>
#include <tuple>

namespace WebStat {
	template<auto Deleter>
	using DeleteWith = decltype([](auto obj) {
		return Deleter(obj);
	});

	template<typename... T>
	auto
	visit(auto && visitor, const std::tuple<T...> & values)
	{
		std::apply(
				[&](auto &&... value) {
					(visitor(value), ...);
				},
				values);
	}

	template<typename... T>
	void
	bindMany(const DB::CommandPtr & cmd, unsigned int firstParam, T &&... param)
	{
		(cmd->bindParam(firstParam++, std::forward<T>(param)), ...);
	}
}
