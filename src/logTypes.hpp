#pragma once

#include <array>
#include <md5.h>
#include <optional>
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

	enum class EntityType : uint8_t {
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

	struct EntityKey {
		EntityHash hash;
		EntityType type;
		constexpr auto operator<=>(const EntityKey &) const noexcept = default;
	};

	struct Entity {
		EntityKey key;
		std::optional<EntityId> id;
		std::string_view value;
	};

	std::string_view & operator>>(std::string_view & input, CLFString & out);
	std::string_view & operator>>(std::string_view & input, QueryString & out);
	std::string_view & operator>>(std::string_view & input, QuotedString & out);
}
