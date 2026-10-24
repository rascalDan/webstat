#include "logTypes.hpp"
#include "string_view_stream.hpp"
#include <algorithm>
#include <stdexcept>

namespace WebStat {
	namespace {
		constexpr int HEX_WIDTH = 2;
		constexpr int HEX_BASE = 16;
		using namespace string_view_stream_helpers::detail;

		using CharMapping = std::pair<char, char>;
		constexpr std::array<CharMapping, 8> BS_MAP = {
				{
						{'"', '"'},
						{'\\', '\\'},
						{'b', '\b'},
						{'f', '\f'},
						{'n', '\n'},
						{'r', '\r'},
						{'t', '\t'},
						{'v', '\v'},
				},
		};
		static_assert(std::ranges::is_sorted(BS_MAP), "Mapped escape chars is not sorted");

		std::string_view &
		parseEscapedString(std::string_view & input, std::string & out)
		{
			const auto getNextSignificantChar = [&input] {
				return std::ranges::find_first_of(input, R"(\")");
			};

			while (!input.empty()) {
				const auto significantChar = getNextSignificantChar();
				if (significantChar == input.end()) {
					throw std::invalid_argument {"No closing double quote"};
				}
				out.append(input.begin(), significantChar);
				input = {significantChar, input.end()};
				if (nextIs(input, '"')) {
					return input;
				}
				if (nextIs(input, '\\')) {
					if (nextIs(input, 'x')) {
						const auto digits = requireWhile(input, ::isxdigit, HEX_WIDTH, HEX_WIDTH);
						if (uint8_t hex = 0;
								std::from_chars(digits.begin(), digits.end(), hex, HEX_BASE).ec == std::errc {}) {
							out.append(1, static_cast<char>(hex));
							continue;
						}
						throw std::invalid_argument {"Unparsable hex data"};
					}
					if (const auto mapped = std::ranges::lower_bound(BS_MAP, input.front(), {}, &CharMapping::first);
							mapped != BS_MAP.end()) {
						input.remove_prefix(1);
						out.append(1, mapped->second);
						continue;
					}
					throw std::invalid_argument {"Invalid escape sequence"};
				}
				throw std::logic_error {"Unexpected next significant char"};
			}
			throw std::invalid_argument {"Never found closing double quote"};
		}
	}

	std::string_view &
	operator>>(std::string_view & input, CLFString & out)
	{
		using namespace string_view_stream_helpers::detail;
		discardWhile(input, isspace);
		requireNext(input, '"');
		if (nextIs(input, '-') && nextIs(input, '"')) {
			// CLF null indicator
			out.reset();
			return input;
		}
		return parseEscapedString(input, out.emplace());
	}

	std::string_view &
	operator>>(std::string_view & input, QueryString & out)
	{
		using namespace string_view_stream_helpers::detail;
		discardWhile(input, isspace);
		requireNext(input, '"');
		if (nextIs(input, '"')) {
			// Query string is empty => null
			out.reset();
			return input;
		}
		requireNext(input, '?');
		return parseEscapedString(input, out.emplace());
	}

	std::string_view &
	operator>>(std::string_view & input, QuotedString & out)
	{
		using namespace string_view_stream_helpers::detail;
		discardWhile(input, isspace);
		requireNext(input, '"');
		return parseEscapedString(input, out);
	}
}
