#include "testing-util.hpp"
#include <fstream>
#include <random>

namespace WebStat {
	MockDB::MockDB() :
		DB::PluginMock<PQ::Mock>("webstat", {SRC_DIR / "schema.sql"}, "user=postgres dbname=postgres") { }

	MockDBPool::MockDBPool(std::string name) : DB::BasicConnectionPool(1, 1), name {std::move(name)} { }

	DB::ConnectionPtr
	MockDBPool::createResource() const
	{
		return DB::MockDatabase::openConnectionTo(name);
	}

	utsname
	getTestUtsName(const std::string_view nodename)
	{
		utsname uts {};
		nodename.copy(uts.nodename, sizeof(uts.nodename));
		return uts;
	}

	LogFile::LogFile(std::filesystem::path where, size_t entries) : path {std::move(where)}
	{
		std::random_device randDevice;
		std::mt19937 generator(randDevice());

		struct Strings {
			std::vector<std::string> vhosts;
			std::vector<std::string> ips;
			std::vector<std::string> paths;
			std::vector<std::string> qss;
			std::vector<std::string> refs;
			std::vector<std::string> uas;
			std::vector<std::string> ct;
		};

		Strings strings;

		auto genIp = [&generator]() {
			static std::uniform_int_distribution<unsigned short> octetDistrib {0, std::numeric_limits<uint8_t>::max()};
			return std::format("{}.{}.{}.{}", octetDistrib(generator), octetDistrib(generator), octetDistrib(generator),
					octetDistrib(generator)

			);
		};
		auto getStrGen = [&generator](size_t minLen, size_t maxLen) {
			return [minLen, maxLen, &generator]() {
				std::uniform_int_distribution<char> charDistrib {'a', 'z'};
				std::uniform_int_distribution<size_t> lenDistrib {minLen, maxLen};
				std::string out;
				std::generate_n(std::back_inserter(out), lenDistrib(generator), [&generator, &charDistrib]() {
					return charDistrib(generator);
				});
				return out;
			};
		};

		for (auto & [out, count, stringGenerator] :
				std::initializer_list<std::tuple<std::vector<std::string> &, size_t, std::function<std::string()>>> {
						{strings.vhosts, 4, getStrGen(6, 20)},
						{strings.ips, 4, genIp},
						{strings.paths, 100, getStrGen(1, 50)},
						{strings.qss, 100, getStrGen(1, 50)},
						{strings.refs, 50, getStrGen(10, 50)},
						{strings.uas, 10, getStrGen(50, 70)},
						{strings.ct, 10, getStrGen(10, 20)},
				}) {
			std::generate_n(std::back_inserter(out), count, stringGenerator);
		}
		strings.qss.emplace_back("");
		strings.refs.emplace_back("-");
		strings.uas.emplace_back("-");

		constexpr size_t MISC_MIN = 1000;
		constexpr size_t MISC_MAX = 10000;
		constexpr uint64_t TICK_START = 1755710158296508;
		std::uniform_int_distribution<size_t> tickDistrib {MISC_MIN, MISC_MAX};
		std::uniform_int_distribution<size_t> sizeDistrib {MISC_MIN, MISC_MAX};
		std::uniform_int_distribution<size_t> durationDistrib {MISC_MIN, MISC_MAX};
		uint64_t tick = TICK_START;
		auto randomString = [&generator](auto & stringSet) {
			std::uniform_int_distribution<size_t> choiceDistrib {0, stringSet.size() - 1};
			return stringSet[choiceDistrib(generator)];
		};

		std::ofstream logfile {path};
		for (size_t line = 0; line < entries; ++line) {
			std::println(logfile, R"LOG({} {} {} GET "/{}" "?{}" HTTP/1.1 200 {} {} "{}" "{}" "{}")LOG",
					randomString(strings.vhosts), randomString(strings.ips), tick += tickDistrib(generator),
					randomString(strings.paths), randomString(strings.qss), sizeDistrib(generator),
					durationDistrib(generator), randomString(strings.refs), randomString(strings.uas),
					randomString(strings.ct));
		}
	}

	LogFile::~LogFile()
	{
		std::filesystem::remove(path);
	}
}
