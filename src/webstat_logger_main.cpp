#include "ingestor.hpp"
#include <pq-connection.h>

int
main(int, char **)
{
	auto dbconn = std::make_shared<PQ::Connection>("dbname=webstat user=webstat");
	WebStat::Ingestor {dbconn}.ingestLog(stdin);
}
