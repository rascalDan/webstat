#pragma once

#include <md5.h>
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
		ContentType,
	};

	using EntityId = int32_t;
	using EntityHash = std::array<uint8_t, MD5_DIGEST_LENGTH>;

	struct Entity {
		EntityHash hash;
		std::optional<EntityId> id;
		EntityType type;
		std::string_view value;
	};
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
