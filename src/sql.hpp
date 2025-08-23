#pragma once

#include <command_fwd.h>
#include <string>

namespace WebStat::SQL {
#define EMBED_DECLARE(Name) \
	extern const std::string Name; \
	extern const DB::CommandOptionsPtr Name##_OPTS

	EMBED_DECLARE(ACCESS_LOG_INSERT);
	EMBED_DECLARE(ENTITY_INSERT);
#undef EMBED_DECLARE
}
