#pragma once

#include <chrono>
#include <command.h>
#include <filesystem>
#include <scn/scan.h>
#include <shared_mutex>
#include <tuple>

namespace WebStat {
	template<auto Deleter> struct DeleteWith {
		auto
		operator()(auto obj)
		{
			return Deleter(obj);
		}
	};

	template<typename... T>
	auto
	visit(auto && visitor, std::tuple<T...> & values)
	{
		std::apply(
				[&](auto &&... value) {
					(visitor(value), ...);
				},
				values);
	}

	using FilePtr = std::unique_ptr<std::FILE, DeleteWith<&fclose>>;

	template<typename... T>
	void
	bindMany(const DB::CommandPtr & cmd, unsigned int firstParam, T &&... param)
	{
		(cmd->bindParam(firstParam++, std::forward<T>(param)), ...);
	}

	namespace detail {
		template<typename Rep, typename Period, typename AddPeriod>
		void
		add(std::chrono::duration<Rep, Period> & subtotal, Rep value)
		{
			if constexpr (requires { subtotal += AddPeriod {value}; }) {
				subtotal += AddPeriod {value};
			}
		}
	}

	template<typename Rep, typename Period>
	std::chrono::duration<Rep, Period>
	parseDuration(std::string_view input)
	{
		static constexpr std::initializer_list<
				std::pair<void (*)(std::chrono::duration<Rep, Period> & subtotal, Rep value), std::string_view>>
				DURATION_UNITS {
						{detail::add<Rep, Period, std::chrono::milliseconds>, "ms"},
						{detail::add<Rep, Period, std::chrono::seconds>, "s"},
						{detail::add<Rep, Period, std::chrono::minutes>, "m"},
						{detail::add<Rep, Period, std::chrono::hours>, "h"},
						{detail::add<Rep, Period, std::chrono::days>, "d"},
						{detail::add<Rep, Period, std::chrono::weeks>, "w"},
				};

		std::chrono::duration<Rep, Period> out {};
		auto inputSubRange = scn::ranges::subrange {input};

		while (auto result = scn::scan<Rep, std::string>(inputSubRange, "{}{:[a-z]}")) {
			const auto & [count, chars] = result->values();
			if (auto unit = std::ranges::find(
						DURATION_UNITS, chars, &std::decay_t<decltype(*DURATION_UNITS.begin())>::second);
					unit != DURATION_UNITS.end()) {
				unit->first(out, count);
			}
			inputSubRange = result->range();
		}

		return out;
	}

	template<typename Clock, typename Dur, typename Rep, typename Period>
	bool
	expired(const std::chrono::time_point<Clock, Dur> lastRun, const std::chrono::duration<Rep, Period> freq,
			const typename Clock::time_point now = Clock::now())
	{
		return lastRun + freq < now;
	}

	template<typename Clock, typename Dur, typename Rep, typename Period>
	bool
	expiredThenSet(std::chrono::time_point<Clock, Dur> & lastRun, const std::chrono::duration<Rep, Period> freq,
			const typename Clock::time_point now = Clock::now())
	{
		if (expired(lastRun, freq, now)) {
			lastRun = now;
			return true;
		}
		return false;
	}

	template<typename ValueType, typename MutexType = std::shared_mutex> class ThreadSafeT {
	public:
		template<typename... P> ThreadSafeT(P &&... params) : value {std::forward<P>(params)...} { }

		template<typename LockedValueType, typename LockType> class Locked {
		public:
			Locked(LockedValueType & valueRef, MutexType & mutex) : value {valueRef}, lock {mutex} { }

			LockedValueType *
			operator->()
			{
				return &value;
			}

		private:
			LockedValueType & value;
			LockType lock;
		};

		Locked<const ValueType, std::shared_lock<MutexType>>
		shared() const
		{
			return {value, mutex};
		}

		Locked<ValueType, std::lock_guard<MutexType>>
		unique()
		{
			return {value, mutex};
		}

		Locked<const ValueType, std::shared_lock<MutexType>>
		operator->() const
		{
			return shared();
		}

		Locked<ValueType, std::lock_guard<MutexType>>
		operator()()
		{
			return unique();
		}

	private:
		ValueType value;
		mutable MutexType mutex;
	};

	std::error_code renameNoExcept(
			const std::filesystem::path & path, const std::filesystem::path & finalPath) noexcept;
}
