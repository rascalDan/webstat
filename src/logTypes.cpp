#include "logTypes.hpp"

namespace {
	namespace {
		constexpr auto BS_MAP = []() {
			std::array<char, 'v' + 1> map {};
			map.at('b') = '\b';
			map.at('f') = '\f';
			map.at('n') = '\n';
			map.at('r') = '\r';
			map.at('t') = '\t';
			map.at('v') = '\v';
			map.at('"') = '"';
			map.at('\\') = '\\';
			return map;
		}();

		scn::scan_expected<scn::ContextType::iterator>
		parseEscapedString(std::string & value, scn::ContextType & ctx, const auto & start)
		{
			ctx.advance_to(start->begin());
			while (true) {
				if (auto closeQuote = scn::scan<>(ctx.range(), R"(")")) {
					return closeQuote->begin();
				}
				if (auto plain = scn::scan<std::string>(ctx.range(), R"({:[^\"]})")) {
					value.append(plain->value());
					ctx.advance_to(plain->begin());
				}
				else if (auto hex = scn::scan<unsigned char>(ctx.range(), R"HEX(\x{:.2x})HEX")) {
					value.append(1, static_cast<char>(hex->value()));
					ctx.advance_to(hex->begin());
				}
				else if (auto escaped = scn::scan<std::string>(ctx.range(), R"ESC(\{:.1[bfnrtv"\]})ESC")) {
					value.append(1, BS_MAP.at(static_cast<unsigned char>(escaped->value().front())));
					ctx.advance_to(escaped->begin());
				}
				else {
					return scn::unexpected(start.error());
				}
			}
			return scn::unexpected(start.error());
		}
	}
}

namespace scn {
	scan_expected<ContextType::iterator>
	scanner<WebStat::QuotedString>::scan(WebStat::QuotedString & value, ContextType & ctx)
	{
		if (auto empty = scn::scan<>(ctx.range(), R"("")")) {
			return empty->begin();
		}

		auto simple = scn::scan<std::string>(ctx.range(), R"("{:[^\"]}")");
		if (simple) {
			value = std::move(simple->value());
			return simple->begin();
		}

		if (auto openQuote = scn::scan<>(ctx.range(), R"(")")) {
			return parseEscapedString(value, ctx, openQuote);
		}
		return unexpected(simple.error());
	}

	scan_expected<ContextType::iterator>
	scanner<WebStat::QueryString>::scan(WebStat::QueryString & value, ContextType & ctx)
	{
		if (auto null = scn::scan<>(ctx.range(), R"("")")) {
			return null->begin();
		}

		auto empty = scn::scan<>(ctx.range(), R"("?")");
		if (empty) {
			value.emplace();
			return empty->begin();
		}

		if (auto openQuoteQM = scn::scan<>(ctx.range(), R"("?)")) {
			value.emplace();
			return parseEscapedString(*value, ctx, openQuoteQM);
		}
		return unexpected(empty.error());
	}

	scan_expected<ContextType::iterator>
	scanner<WebStat::CLFString>::scan(WebStat::CLFString & value, ContextType & ctx)
	{
		if (auto null = scn::scan<>(ctx.range(), R"("-")")) {
			return null->begin();
		}

		return scn::scanner<WebStat::QuotedString>::scan(value.emplace(), ctx);
	}
}
