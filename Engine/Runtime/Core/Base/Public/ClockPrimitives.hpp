#pragma once

#include <chrono>
#include <ratio>
#include <utility>

namespace rox::core
{
	using MonotonicClock = std::chrono::steady_clock;
	using WallClock = std::chrono::system_clock;

	using Nanoseconds = std::chrono::nanoseconds;
	using Microseconds = std::chrono::microseconds;
	using Milliseconds = std::chrono::milliseconds;
	using Seconds = std::chrono::seconds;
	using Minutes = std::chrono::minutes;

	template <typename T, typename Period = std::ratio<1>> using ClockDuration = std::chrono::duration<T, Period>;

	using Milli = std::milli;

	template <typename... TTypes, typename... TArgs>
	constexpr auto DurationCast(TArgs&&... args) noexcept(noexcept(std::chrono::duration_cast<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::chrono::duration_cast<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::chrono::duration_cast<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto DurationCeil(TArgs&&... args) noexcept(noexcept(std::chrono::ceil<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::chrono::ceil<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::chrono::ceil<TTypes...>(std::forward<TArgs>(args)...);
	}
}
