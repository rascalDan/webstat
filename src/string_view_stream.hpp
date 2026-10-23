#pragma once
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace WebStat::string_view_stream_helpers {
	template<typename Pred>
	concept CharPred = requires(const Pred & pred, std::string_view::value_type chr) {
		{ pred(chr) } -> std::convertible_to<bool>;
	};

	namespace detail {
		[[nodiscard]]
		std::string_view
		getWhile(std::string_view & input, CharPred auto pred)
		{
			const auto end = std::ranges::find_if_not(input, pred);
			const std::string_view out {input.begin(), end};
			input = {end, input.end()};
			return out;
		}

		[[nodiscard]]
		std::string_view
		requireWhile(std::string_view & input, CharPred auto pred)
		{
			if (const auto end = std::ranges::find_if_not(input, pred); end != input.begin()) {
				const std::string_view out {input.begin(), end};
				input = {end, input.end()};
				return out;
			}
			throw std::invalid_argument {"Unexpected input"};
		}

		[[nodiscard]]
		std::string_view
		requireWhile(std::string_view & input, CharPred auto pred, size_t limit)
		{
			if (const auto end = std::ranges::find_if_not(input.substr(0, limit), pred); end != input.begin()) {
				const std::string_view out {input.begin(), end};
				input = {end, input.end()};
				return out;
			}
			throw std::invalid_argument {"Unexpected input"};
		}

		void requireNext(std::string_view & input, char chr);
		[[nodiscard]] bool nextIs(std::string_view & input, char chr);

		void
		discardWhile(std::string_view & input, CharPred auto pred)
		{
			if (const auto end = std::ranges::find_if_not(input, pred); end != input.end()) {
				input = {end, input.end()};
			}
		}
	}

	[[nodiscard]]
	auto
	operator%(std::string_view & out, CharPred auto && pred)
	{
		return std::tie(out, pred);
	}

	std::string_view & operator>>(std::string_view & input, std::string_view & out);
	std::string_view & operator>>(std::string_view & input, std::integral auto & out);

	template<CharPred Pred>
	std::string_view &
	operator>>(std::string_view & input, const std::tuple<std::string_view &, Pred &> & spec)
	{
		using namespace detail;
		auto && [out, pred] = spec;
		discardWhile(input, ::isspace);
		out = requireWhile(input, pred);
		return input;
	}
}
