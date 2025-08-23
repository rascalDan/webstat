#define BOOST_TEST_MODULE ingest
#include <boost/test/data/test_case.hpp>
#include <boost/test/unit_test.hpp>

#include <dbpp-postgresql/pq-mock.h>
#include <filesystem>
#include <ingestor.hpp>
#include <mockDatabase.h>

#define XSTR(s) STR(s)
#define STR(s) #s
const std::filesystem::path SRC_DIR(XSTR(SRC));
const std::filesystem::path TEST_DIR(XSTR(TEST));
#undef XSTR
#undef STR

class Mock : public DB::PluginMock<PQ::Mock> {
public:
	Mock() : DB::PluginMock<PQ::Mock>("webstat", {SRC_DIR / "schema.sql"}, "user=postgres dbname=postgres") { }
};

BOOST_GLOBAL_FIXTURE(Mock);

using ScanValues = std::remove_cvref_t<decltype(std::declval<WebStat::Ingestor::ScanResult>()->values())>;
template<typename Out> using ParseData = std::tuple<std::string_view, Out>;
template<auto Deleter>
using DeleteWith = decltype([](auto obj) {
	return Deleter(obj);
});
using FilePtr = std::unique_ptr<std::FILE, DeleteWith<&fclose>>;

namespace std {
	template<typename T>
	ostream &
	operator<<(ostream & strm, const std::optional<T> & value)
	{
		if (value) {
			strm << *value;
		}
		return strm;
	}

	template<typename... T>
	ostream &
	operator<<(ostream & strm, const std::tuple<T...> & values)
	{
		return std::apply(
				[&strm](auto &&... elems) -> decltype(auto) {
					return ((strm << elems << '\n'), ...);
				},
				values);
	}
}

BOOST_DATA_TEST_CASE(QuotedStringsGood,
		boost::unit_test::data::make<ParseData<WebStat::QuotedString>>({
				{R"("")", ""},
				{R"("-")", "-"},
				{R"(".")", "."},
				{R"("/url/path")", "/url/path"},
		}),
		input, expected)
{
	const auto result = scn::scan<WebStat::QuotedString>(input, "{}");
	BOOST_REQUIRE(result);
	BOOST_CHECK_EQUAL(result->value(), expected);
}

BOOST_DATA_TEST_CASE(QuotedStringsBad,
		boost::unit_test::data::make<std::string_view>({
				R"()",
				R"(-)",
				R"(word)",
				R"(/url/path)",
		}),
		input)
{
	BOOST_REQUIRE(!scn::scan<WebStat::QuotedString>(input, "{}"));
}

BOOST_DATA_TEST_CASE(QueryStringsGood,
		boost::unit_test::data::make<ParseData<WebStat::QueryString>>({
				{R"("")", std::nullopt},
				{R"("?")", ""},
				{R"("?something")", "something"},
				{R"("?some=thing")", "some=thing"},
				{R"("?some=thing&other=thing")", "some=thing&other=thing"},
		}),
		input, expected)
{
	const auto result = scn::scan<WebStat::QueryString>(input, "{}");
	BOOST_REQUIRE(result);
	BOOST_CHECK_EQUAL(result->value(), expected);
}

BOOST_DATA_TEST_CASE(QueryStringsBad,
		boost::unit_test::data::make<std::string_view>({
				R"()",
				R"("-")",
				R"(".")",
				R"(-)",
				R"(word)",
				R"(/url/path)",
		}),
		input)
{
	BOOST_REQUIRE(!scn::scan<WebStat::QueryString>(input, "{}"));
}

BOOST_TEST_DECORATOR(*boost::unit_test::timeout(1))

BOOST_DATA_TEST_CASE(CLFStringsDecode,
		boost::unit_test::data::make<ParseData<std::string>>({
				{"", ""},
				{"plain", "plain"},
				{R"(hex\x41)", "hexA"},
				{R"(hex\x4141)", "hexA41"},
				{R"(hex\x41\x41)", "hexAA"},
				{R"(hex\t\x41)", "hex\tA"},
		}),
		input, expected)
{
	std::string value {input};
	scn::scanner<WebStat::CLFString>::decode(value);
	BOOST_CHECK_EQUAL(value, expected);
}

BOOST_TEST_DECORATOR(*boost::unit_test::depends_on("CLFStringsDecode"))

BOOST_DATA_TEST_CASE(CLFStringsGood,
		boost::unit_test::data::make<ParseData<WebStat::CLFString>>({
				{R"("")", ""},
				{R"("-")", std::nullopt},
				{R"("?")", "?"},
				{R"(".")", "."},
				{R"("something")", "something"},
				{R"("https://google.com")", "https://google.com"},
				{R"("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")",
						R"(Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36)"},
		}),
		input, expected)
{
	const auto result = scn::scan<WebStat::CLFString>(input, "{}");
	BOOST_REQUIRE(result);
	BOOST_CHECK_EQUAL(result->value(), expected);
}

BOOST_DATA_TEST_CASE(CLFStringsBad,
		boost::unit_test::data::make<std::string_view>({
				R"()",
				R"(-)",
				R"(word)",
				R"(/url/path)",
		}),
		input)
{
	BOOST_REQUIRE(!scn::scan<WebStat::CLFString>(input, "{}"));
}

constexpr std::string_view LOGLINE1
		= R"LOG(git.randomdan.homeip.net 98.82.40.168 1755561576768318 GET "/repo/gentoobrowse-api/commit/gentoobrowse-api/unittests/fixtures/756569aa764177340726dd3d40b41d89b11b20c7/app-crypt/pdfcrack/Manifest" "?h=gentoobrowse-api-0.9.1&id=a2ed3fd30333721accd4b697bfcb6cc4165c7714" HTTP/1.1 200 1884 107791 "-" "Mozilla/5.0 AppleWebKit/537.36 (KHTML, like Gecko; compatible; Amazonbot/0.1; +https://developer.amazon.com/support/amazonbot) Chrome/119.0.6045.214 Safari/537.36")LOG";
constexpr std::string_view LOGLINE2
		= R"LOG(www.randomdan.homeip.net 43.128.84.166 1755561575973204 GET "/app-dicts/myspell-et/Manifest" "" HTTP/1.1 200 312 10369 "https://google.com" "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/104.0.0.0 Safari/537.36")LOG";

BOOST_TEST_DECORATOR(*boost::unit_test::depends_on("QuotedStringsGood"))
BOOST_TEST_DECORATOR(*boost::unit_test::depends_on("QueryStringsGood"))
BOOST_TEST_DECORATOR(*boost::unit_test::depends_on("CLFStringsGood"))

BOOST_DATA_TEST_CASE(ExtractFields,
		boost::unit_test::data::make<ParseData<WebStat::Ingestor::ScanValues>>({
				{LOGLINE1,
						{"git.randomdan.homeip.net", "98.82.40.168", 1755561576768318, "GET",
								R"(/repo/gentoobrowse-api/commit/gentoobrowse-api/unittests/fixtures/756569aa764177340726dd3d40b41d89b11b20c7/app-crypt/pdfcrack/Manifest)",
								R"(h=gentoobrowse-api-0.9.1&id=a2ed3fd30333721accd4b697bfcb6cc4165c7714)", "HTTP/1.1",
								200, 1884, 107791, std::nullopt,
								R"(Mozilla/5.0 AppleWebKit/537.36 (KHTML, like Gecko; compatible; Amazonbot/0.1; +https://developer.amazon.com/support/amazonbot) Chrome/119.0.6045.214 Safari/537.36)"}},
				{LOGLINE2,
						{"www.randomdan.homeip.net", "43.128.84.166", 1755561575973204, "GET",
								"/app-dicts/myspell-et/Manifest", std::nullopt, "HTTP/1.1", 200, 312, 10369,
								"https://google.com",
								R"(Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/104.0.0.0 Safari/537.36)"}},
		}),
		input, expected)
{
	const auto result = WebStat::Ingestor::scanLogLine(input);
	BOOST_REQUIRE(result);
	BOOST_CHECK_EQUAL(result->values(), expected);
}
