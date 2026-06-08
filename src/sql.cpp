#include "sql.hpp"
#include <command.h>
#include <dbpp-postgresql/pq-command.h>

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
	const std::string SELECT_UNINSERTABLE {
#embed "sql/selectUninsertableLines.sql"
	};
	const std::string DELETE_ENTITY {
#embed "sql/deleteEntity.sql"
	};
	const std::string MARK_ENTITY_RETRIED {
#embed "sql/markEntityRetried.sql"
	};
	const std::string SET_ENTITY_TYPE {
#embed "sql/setEntityType.sql"
	};
#define HASH_OPTS(VAR) \
	const DB::CommandOptionsPtr VAR##_OPTS \
			= std::make_shared<PQ::CommandOptions>(std::hash<std::string> {}(VAR), 35, false)
	HASH_OPTS(ACCESS_LOG_INSERT);
	HASH_OPTS(ACCESS_LOG_PURGE_OLD);
	HASH_OPTS(ENTITY_INSERT);
	HASH_OPTS(ENTITY_UPDATE_DETAIL);
	HASH_OPTS(HOST_UPSERT);
	const DB::CommandOptionsPtr SELECT_UNINSERTABLE_OPTS
			= std::make_shared<PQ::CommandOptions>(std::hash<std::string> {}(SELECT_UNINSERTABLE), 35, false);
	HASH_OPTS(DELETE_ENTITY);
	HASH_OPTS(MARK_ENTITY_RETRIED);
	HASH_OPTS(SET_ENTITY_TYPE);
#undef HASH_OPTS
}
