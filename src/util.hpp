#pragma once

#include <tuple>

namespace WebStat {
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
}
