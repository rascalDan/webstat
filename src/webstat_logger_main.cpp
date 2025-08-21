#include "ingestor.hpp"

int
main(int, char **)
{
	WebStat::Ingestor {}.ingestLog(stdin);
}
