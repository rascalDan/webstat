#include "test-util.hpp"

namespace WebStat {
	MockDB::MockDB() :
		DB::PluginMock<PQ::Mock>("webstat", {SRC_DIR / "schema.sql"}, "user=postgres dbname=postgres") { }

	MockDBPool::MockDBPool(std::string name) : DB::BasicConnectionPool(1, 1), name {std::move(name)} { }

	DB::ConnectionPtr
	MockDBPool::createResource() const
	{
		return DB::MockDatabase::openConnectionTo(name);
	}
}
