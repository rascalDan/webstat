#include "test-util.hpp"

namespace WebStat {
	MockDB::MockDB() :
		DB::PluginMock<PQ::Mock>("webstat", {SRC_DIR / "schema.sql"}, "user=postgres dbname=postgres") { }
}
