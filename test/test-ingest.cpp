#define BOOST_TEST_MODULE ingest
#include <boost/test/data/test_case.hpp>
#include <boost/test/unit_test.hpp>

#include "test-util.hpp"
#include <selectcommandUtil.impl.h>

#include <ingestor.hpp>
#include <uaLookup.hpp>

namespace {
	using namespace WebStat;
	BOOST_GLOBAL_FIXTURE(MockDB);
}

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
					return ((strm << '{' << elems << ", "), ...) << '}';
				},
				values);
	}

	template<typename... T>
	ostream &
	operator<<(ostream & strm, const DB::Row<T...> & row)
	{
		return [&]<size_t... Field>(std::integer_sequence<size_t, Field...>) -> decltype(auto) {
			return ((strm << '{' << row.template get<Field>() << ", "), ...) << '}';
		}(std::make_integer_sequence<size_t, sizeof...(T)>());
	}
}

namespace DB {
	template<typename... T>
	bool
	operator!=(const Row<T...> & row, const std::tuple<T...> & expected)
	{
		return [&]<size_t... Field>(std::integer_sequence<size_t, Field...>) {
			return std::make_tuple(row.template get<Field>()...);
		}(std::make_integer_sequence<size_t, sizeof...(T)>())
				!= expected;
	}
}

BOOST_DATA_TEST_CASE(QuotedStringsGood,
		boost::unit_test::data::make<WebStat::ParseData<WebStat::QuotedString>>({
				{R"("")", ""},
				{R"("-")", "-"},
				{R"(".")", "."},
				{R"("/url/path")", "/url/path"},
				{R"("hex\x41")", "hexA"},
				{R"("hex\x4141")", "hexA41"},
				{R"("hex\x41\x41")", "hexAA"},
				{R"("hex\t\x41")", "hex\tA"},
				{R"("/packages/dev-php/pecl-uploadprogress(),')\"((,,")",
						R"LOG(/packages/dev-php/pecl-uploadprogress(),')"((,,)LOG"},
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
		boost::unit_test::data::make<WebStat::ParseData<WebStat::QueryString>>({
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

BOOST_DATA_TEST_CASE(CLFStringsGood,
		boost::unit_test::data::make<WebStat::ParseData<WebStat::CLFString>>({
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
		boost::unit_test::data::make<WebStat::ParseData<WebStat::Ingestor::ScanValues>>({
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

BOOST_AUTO_TEST_CASE(ExtractFieldsEdgeCasesUnparsable3580673700)
{
	const auto result = WebStat::Ingestor::scanLogLine(
			R"LOG(gentoobrowse.randomdan.homeip.net 5.183.129.58 1759960912510520 GET "/packages/dev-php/pecl-uploadprogress(),')\"((,," "" HTTP/1.1 404 0 10051 "-" "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/138.0.0.0 Safari/537.36")LOG");
	BOOST_REQUIRE(result);
	BOOST_CHECK_EQUAL(std::get<4>(result->values()), R"LOG(/packages/dev-php/pecl-uploadprogress(),')"((,,)LOG");
}

BOOST_AUTO_TEST_CASE(ExtractFieldsEdgeCasesUnparsable3603068405)
{
	const auto result = WebStat::Ingestor::scanLogLine(
			R"LOG(gentoobrowse.randomdan.homeip.net 5.183.129.58 1759960912705682 GET "/packages/dev-php/pecl-uploadprogress'yqFSRA<'\">yuezhx" "" HTTP/1.1 404 0 19143 "-" "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/138.0.0.0 Safari/537.36")LOG");
	BOOST_REQUIRE(result);
	BOOST_CHECK_EQUAL(std::get<4>(result->values()), R"LOG(/packages/dev-php/pecl-uploadprogress'yqFSRA<'">yuezhx)LOG");
}

class TestIngestor : public WebStat::Ingestor {
public:
	TestIngestor() :
		WebStat::Ingestor {WebStat::getTestUtsName("test-hostname"), std::make_shared<MockDBPool>("webstat"),
				{
						.userAgentAPI = FIXTURE_URL_BASE + "/userAgent.json",
						.fallbackDir = std::format("/tmp/webstat-{}", getpid()),
				}}
	{
		std::filesystem::create_directories(settings.fallbackDir);
	}

	~TestIngestor() override
	{
		std::filesystem::remove_all(settings.fallbackDir);
	}

	SPECIAL_MEMBERS_DELETE(TestIngestor);
};

BOOST_FIXTURE_TEST_SUITE(I, TestIngestor);

BOOST_TEST_DECORATOR(*boost::unit_test::depends_on("ExtractFields"))

BOOST_DATA_TEST_CASE(StoreLogLine,
		boost::unit_test::data::make({
				LOGLINE1,
				LOGLINE2,
		}),
		line)
{
	ingestLogLine(DB::MockDatabase::openConnectionTo("webstat").get(), line);
	BOOST_CHECK_EQUAL(linesRead, 0);
	BOOST_CHECK_EQUAL(linesParsed, 1);
	BOOST_CHECK_EQUAL(linesDiscarded, 0);
}

BOOST_AUTO_TEST_CASE(StoreLog, *boost::unit_test::depends_on("I/StoreLogLine"))
{
	WebStat::LogFile log {"/tmp/store-log-fixture.log", 10};
	WebStat::FilePtr input {fopen(log.path.c_str(), "r")};
	BOOST_REQUIRE(input);
	ingestLog(input.get());
	BOOST_CHECK_EQUAL(linesRead, 10);
	BOOST_CHECK_EQUAL(linesParsed, 10);
	BOOST_CHECK_EQUAL(linesDiscarded, 0);
}

BOOST_AUTO_TEST_CASE(ParkLogLine)
{
	parkLogLine(LOGLINE1);
	BOOST_CHECK_EQUAL(linesParked, 1);
	const auto path = settings.fallbackDir / "parked-3377916038.log";
	BOOST_TEST_INFO(path);
	BOOST_REQUIRE(std::filesystem::exists(path));
	BOOST_CHECK_EQUAL(std::filesystem::file_size(path), LOGLINE1.length());
}

BOOST_TEST_DECORATOR(*boost::unit_test::depends_on("I/ParkLogLine"))

BOOST_AUTO_TEST_CASE(ParkLogLineOnError)
{
	BOOST_REQUIRE_NO_THROW(dbpool->get()->execute("SET search_path = ''"));
	BOOST_REQUIRE_NO_THROW(ingestLogLine(LOGLINE1));
	BOOST_CHECK_EQUAL(linesParked, 1);
}

BOOST_AUTO_TEST_CASE(IngestParked, *boost::unit_test::depends_on("I/ParkLogLine"))
{
	parkLogLine(LOGLINE1);
	BOOST_REQUIRE_EQUAL(linesParked, 1);
	BOOST_REQUIRE_EQUAL(linesParsed, 0);
	jobIngestParkedLines();
	BOOST_CHECK_EQUAL(linesParsed, 1);
	BOOST_CHECK_EQUAL(linesDiscarded, 0);
	BOOST_CHECK(!std::filesystem::exists(settings.fallbackDir / "parked-3377916038.log"));
}

BOOST_AUTO_TEST_CASE(IngestParkedJob, *boost::unit_test::depends_on("I/IngestParked"))
{
	const auto now = JobLastRunTime::clock::now();
	lastRunIngestParkedLines = now - 1s;
	parkLogLine(LOGLINE1);

	runJobsIdle();
	BOOST_REQUIRE_EQUAL(linesParked, 1);
	BOOST_REQUIRE_EQUAL(linesParsed, 0);
	BOOST_CHECK_EQUAL(lastRunIngestParkedLines, now - 1s);

	lastRunIngestParkedLines = now - settings.freqIngestParkedLines + 2s;
	BOOST_REQUIRE_EQUAL(linesParked, 1);
	BOOST_REQUIRE_EQUAL(linesParsed, 0);
	BOOST_CHECK_EQUAL(lastRunIngestParkedLines, now - settings.freqIngestParkedLines + 2s);

	lastRunIngestParkedLines = now - settings.freqIngestParkedLines - 1s;
	runJobsIdle();
	BOOST_CHECK_EQUAL(linesParsed, 1);
	BOOST_CHECK_EQUAL(linesDiscarded, 0);
	BOOST_CHECK_GE(lastRunIngestParkedLines, now);
	BOOST_CHECK(!std::filesystem::exists(settings.fallbackDir / "parked-3377916038.log"));
}

BOOST_AUTO_TEST_CASE(JobErrorRescheduler, *boost::unit_test::depends_on("I/IngestParkedJob"))
{
	const auto now = JobLastRunTime::clock::now();
	lastRunIngestParkedLines = now - settings.freqIngestParkedLines - 1s;
	parkLogLine(LOGLINE1);
	std::filesystem::permissions(settings.fallbackDir / "parked-3377916038.log", std::filesystem::perms::owner_write);
	runJobsIdle();
	BOOST_CHECK(std::filesystem::exists(settings.fallbackDir / "parked-3377916038.log"));
	BOOST_CHECK_GE(lastRunIngestParkedLines, now - (settings.freqIngestParkedLines / 2) - 1s);
	BOOST_CHECK_LE(lastRunIngestParkedLines, now - (settings.freqIngestParkedLines / 2) + 1s);
}

BOOST_AUTO_TEST_CASE(FetchMockUserAgentDetail)
{
	const auto uaDetailReq = WebStat::curlGetUserAgentDetail(0,
			R"(Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36)",
			settings.userAgentAPI.c_str());
	BOOST_REQUIRE(uaDetailReq);
	BOOST_REQUIRE_EQUAL(CURLE_OK, curl_easy_perform(uaDetailReq->hnd.get()));

	BOOST_TEST_CONTEXT(uaDetailReq->result) {
		BOOST_CHECK(uaDetailReq->result.contains(R"("agent_type":)"));
		BOOST_CHECK(uaDetailReq->result.contains(R"("os_type":)"));
		BOOST_CHECK(uaDetailReq->result.contains(R"("Chrome")"));
	}
}

BOOST_AUTO_TEST_CASE(DiscardUnparsable)
{
	BOOST_REQUIRE_NO_THROW(ingestLogLine("does not parse"));
	auto dbconn = dbpool->get();
	auto select = dbconn->select("SELECT id::bigint, value FROM entities WHERE type = 'unparsable_line'");
	constexpr std::array<std::tuple<Crc32Value, std::string_view>, 1> EXPECTED {{
			{1664299262, "does not parse"},
	}};
	auto rows = select->as<Crc32Value, std::string_view>();
	BOOST_CHECK_EQUAL_COLLECTIONS(rows.begin(), rows.end(), EXPECTED.begin(), EXPECTED.end());
}

BOOST_AUTO_TEST_SUITE_END();

BOOST_AUTO_TEST_CASE(FetchRealUserAgentDetail, *boost::unit_test::disabled())
{
	const auto uaDetailReq = WebStat::curlGetUserAgentDetail(0,
			R"(Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36)",
			"http://useragentstring.com");
	BOOST_REQUIRE(uaDetailReq);
	BOOST_REQUIRE_EQUAL(CURLE_OK, curl_easy_perform(uaDetailReq->hnd.get()));

	BOOST_TEST_CONTEXT(uaDetailReq->result) {
		BOOST_CHECK(uaDetailReq->result.starts_with("{"));
		BOOST_CHECK(uaDetailReq->result.ends_with("}"));
		BOOST_CHECK(uaDetailReq->result.contains(R"("agent_type":)"));
		BOOST_CHECK(uaDetailReq->result.contains(R"("os_type":)"));
	}
}
