#include "logTypes.hpp"

namespace scn {
	scan_expected<typename ContextType::iterator>
	scanner<WebStat::QuotedString>::scan(WebStat::QuotedString & value, ContextType & ctx)
	{
		if (auto empty = scn::scan<>(ctx.range(), R"("")")) {
			return empty->begin();
		}

		auto result = scn::scan<std::string>(ctx.range(), R"("{:[^"]}")");
		if (!result) {
			return unexpected(result.error());
		}
		value = result->value();
		return result->begin();
	}

	scan_expected<typename ContextType::iterator>
	scanner<WebStat::QueryString>::scan(WebStat::QueryString & value, ContextType & ctx)
	{
		if (auto null = scn::scan<>(ctx.range(), R"("")")) {
			return null->begin();
		}

		if (auto empty = scn::scan<>(ctx.range(), R"("?")")) {
			value.emplace();
			return empty->begin();
		}

		auto result = scn::scan<std::string>(ctx.range(), R"("?{:[^"]}")");
		if (!result) {
			return unexpected(result.error());
		}
		value = result->value();
		return result->begin();
	}

	scan_expected<typename ContextType::iterator>
	scanner<WebStat::CLFString>::scan(WebStat::CLFString & value, ContextType & ctx)
	{
		if (auto empty = scn::scan<>(ctx.range(), R"("")")) {
			value.emplace();
			return empty->begin();
		}

		if (auto null = scn::scan<>(ctx.range(), R"("-")")) {
			return null->begin();
		}

		auto result = scn::scan<std::string>(ctx.range(), R"("{:[^"]}")");
		if (!result) {
			return unexpected(result.error());
		}
		value = result->value();
		decode(*value);
		return result->begin();
	}

	void
	scanner<WebStat::CLFString>::decode(std::string & value)
	{
		static constexpr auto BS_MAP = []() {
			std::array<char, 128> map {};
			map['f'] = '\f';
			map['n'] = '\n';
			map['r'] = '\r';
			map['t'] = '\t';
			map['v'] = '\v';
			map['"'] = '"';
			map['\\'] = '\\';
			return map;
		}();

		if (auto src = std::ranges::find(value, '\\'); src != value.end()) {
			auto dest = src;
			while (src != value.cend()) {
				if (*src == '\\') {
					const std::string_view escaped {++src, value.end()};
					if (auto chr = BS_MAP[static_cast<unsigned char>(*src)]) {
						*dest++ = chr;
						src++;
					}
					else if (auto hex = scn::scan<unsigned char>(escaped, R"(x{:.2x})")) {
						*dest++ = static_cast<char>(hex->value());
						src += 3;
					}
				}
				else {
					*dest++ = *src++;
				}
			}
			value.erase(dest, value.end());
		}
	}
}
