#define BOOST_TEST_MODULE ingest
#include <boost/test/data/test_case.hpp>
#include <boost/test/unit_test.hpp>

#include <util.hpp>

namespace WebStat {
}

using DurationParserData = std::tuple<std::string_view, std::chrono::milliseconds>;

BOOST_TEST_DECORATOR(*boost::unit_test::timeout(1))

BOOST_DATA_TEST_CASE(durationParser,
		boost::unit_test::data::make<DurationParserData>({
				{"", std::chrono::milliseconds {0}},
				{"123ms", std::chrono::milliseconds {123}},
				{"45s", std::chrono::seconds {45}},
				{"10m", std::chrono::minutes {10}},
				{"7h", std::chrono::hours {7}},
				{"2d", std::chrono::days {2}},
				{"7w", std::chrono::weeks {7}},
				{"1w4d3h45m10s1ms",
						std::chrono::weeks {1} + std::chrono::days {4} + std::chrono::hours {3}
								+ std::chrono::minutes {45} + std::chrono::seconds {10}
								+ std::chrono::milliseconds {1}},
		}),
		input, expected)
{
	BOOST_CHECK_EQUAL(expected, (WebStat::parseDuration<std::intmax_t, std::milli>(input)));
}
