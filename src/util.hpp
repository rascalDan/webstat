#pragma once

#include <tuple>

namespace WebStat {
	template<typename... T>
	auto
	visitSum(auto && visitor, const std::tuple<T...> & values)
	{
		return std::apply(
				[&](auto &&... value) {
					return (visitor(value) + ...);
				},
				values);
	}
}
