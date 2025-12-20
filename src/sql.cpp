#include "sql.hpp"
#include <command.h>

namespace WebStat::SQL {
	// ccache doesn't play nicely with #embed
	// https://github.com/ccache/ccache/issues/1540
	// ccache:disable

	const std::string ACCESS_LOG_INSERT {
#embed "sql/accessLogInsert.sql"
	};
	const std::string ACCESS_LOG_PURGE_OLD {
#embed "sql/accessLogPurgeOld.sql"
	};
	const std::string ENTITY_INSERT {
#embed "sql/entityInsert.sql"
	};
	const std::string ENTITY_UPDATE_DETAIL {
#embed "sql/entityUpdateDetail.sql"
	};
	const std::string HOST_UPSERT {
#embed "sql/hostUpsert.sql"
	};
#define HASH_OPTS(VAR) \
	const DB::CommandOptionsPtr VAR##_OPTS = std::make_shared<DB::CommandOptions>(std::hash<std::string> {}(VAR))
	HASH_OPTS(ACCESS_LOG_INSERT);
	HASH_OPTS(ACCESS_LOG_PURGE_OLD);
	HASH_OPTS(ENTITY_INSERT);
	HASH_OPTS(ENTITY_UPDATE_DETAIL);
	HASH_OPTS(HOST_UPSERT);
#undef HASH_OPTS
}
