#include "string_view_stream.hpp"
#include <cstdint>
#include <functional>

namespace WebStat::string_view_stream_helpers {
	namespace detail {
		void
		requireNext(std::string_view & input, char chr)
		{
			if (input.empty() || input.front() != chr) {
				throw std::invalid_argument {"Unexpected input"};
			}
			input.remove_prefix(1);
		}

		bool
		nextIs(std::string_view & input, char chr)
		{
			if (input.empty() || input.front() != chr) {
				return false;
			}
			input.remove_prefix(1);
			return true;
		}

	}

	std::string_view &
	operator>>(std::string_view & input, std::string_view & out)
	{
		using namespace string_view_stream_helpers::detail;
		discardWhile(input, isspace);
		out = requireWhile(input, std::not_fn(isspace));
		return input;
	}

	std::string_view &
	operator>>(std::string_view & input, std::integral auto & out)
	{
		using namespace string_view_stream_helpers::detail;
		discardWhile(input, isspace);
		const auto digits {requireWhile(input, ::isdigit)};
		if (std::from_chars(digits.begin(), digits.end(), out).ec != std::errc {}) {
			throw std::invalid_argument {"Unparsable integral data"};
		}
		return input;
	}

	template std::string_view & operator>>(std::string_view & input, int64_t & out);
	template std::string_view & operator>>(std::string_view & input, uint64_t & out);
	template std::string_view & operator>>(std::string_view & input, uint16_t & out);

}
