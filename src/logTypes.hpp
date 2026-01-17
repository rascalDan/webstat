#pragma once

#include <optional>
#include <scn/scan.h>
#include <string>

namespace WebStat {
	struct QuotedString : std::string {
		using std::string::string;
		using std::string::operator=;
	};

	struct QueryString : std::optional<std::string> {
		using std::optional<std::string>::optional;
		using std::optional<std::string>::operator=;
		bool operator<=>(const QueryString &) const = default;
	};

	struct CLFString : std::optional<QuotedString> {
		using std::optional<QuotedString>::optional;
		using std::optional<QuotedString>::operator=;
		bool operator<=>(const CLFString &) const = default;
	};

	enum class EntityType : std::uint8_t {
		Host,
		VirtualHost,
		Path,
		QueryString,
		Referrer,
		UserAgent,
		UnparsableLine,
		UninsertableLine,
	};

	using Crc32Value = uint32_t;
	using Entity = std::tuple<Crc32Value, EntityType, std::string_view>;
}

namespace scn {
	using ContextType = scn::v4::basic_scan_context<scn::v4::detail::buffer_range_tag, char>;

	template<> struct scanner<WebStat::QuotedString> : scanner<std::string, char> {
		static scan_expected<typename ContextType::iterator> scan(WebStat::QuotedString & value, ContextType & ctx);
	};

	template<> struct scanner<WebStat::QueryString> : scanner<std::string, char> {
		static scan_expected<typename ContextType::iterator> scan(WebStat::QueryString & value, ContextType & ctx);
	};

	template<> struct scanner<WebStat::CLFString> : scanner<std::string, char> {
		static scan_expected<typename ContextType::iterator> scan(WebStat::CLFString & value, ContextType & ctx);
	};
}
