#define BOOST_TEST_MODULE util
#include <boost/test/data/test_case.hpp>
#include <boost/test/unit_test.hpp> // IWYU pragma: keep

#include <util.hpp>

namespace {
	using DurationParserData = std::tuple<std::string_view, std::chrono::milliseconds>;

	BOOST_TEST_DECORATOR(*boost::unit_test::timeout(1))

	BOOST_DATA_TEST_CASE(durationParser,
			boost::unit_test::data::make<DurationParserData>({
					{"", {}},
					{" ", {}},
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
					{" 5 m 22 s ", std::chrono::seconds {322}},
					{" 22 s 5 m ", std::chrono::seconds {322}},
			}),
			input, expected)
	{
		BOOST_CHECK_EQUAL(expected, (WebStat::parseDuration<std::intmax_t, std::milli>(input)));
	}

	BOOST_DATA_TEST_CASE(durationParserBad,
			boost::unit_test::data::make<std::string_view>({
					"word",
					" word ",
					"1w2d3t",
					"1w2t3d",
					"1t2w3d",
			}),
			input)
	{
		BOOST_CHECK_THROW((WebStat::parseDuration<std::intmax_t, std::milli>(input)), std::invalid_argument);
	}
}
