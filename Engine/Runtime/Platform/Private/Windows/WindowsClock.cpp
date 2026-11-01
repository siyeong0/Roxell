#include <WindowsClock.h>
#include <NumericLimits.hpp>
#include <Assertion.h>
#include <Log.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace rox::os
{
	namespace
	{
		constexpr core::uint64 NANOSECONDS_PER_SECOND = 1'000'000'000;
		constexpr core::uint64 WINDOWS_TICKS_PER_MICROSECOND = 10;
		constexpr core::uint64 WINDOWS_TICKS_AT_UNIX_EPOCH = 116'444'736'000'000'000;

		void addFraction(core::uint64& quotient, core::uint64& remainder, core::uint64 addedQuotient, core::uint64 addedRemainder,
			core::uint64 divisor) noexcept
		{
			ASSERT(remainder < divisor, "A scaled-time remainder must be smaller than its divisor.");
			ASSERT(addedRemainder < divisor, "An added scaled-time remainder must be smaller than its divisor.");
			ASSERT(quotient <= rox::core::NumericLimits<core::uint64>::max() - addedQuotient, "A scaled-time quotient must fit in uint64.");

			quotient += addedQuotient;

			if (remainder >= divisor - addedRemainder)
			{
				remainder -= divisor - addedRemainder;
				ASSERT(quotient < rox::core::NumericLimits<core::uint64>::max(), "A scaled-time carry must fit in uint64.");
				++quotient;

				return;
			}

			remainder += addedRemainder;
		}

		core::uint64 multiplyDivideFloor(core::uint64 multiplicand, core::uint64 multiplier, core::uint64 divisor) noexcept
		{
			ASSERT(divisor > 0, "Integer time scaling requires a non-zero divisor.");
			ASSERT(multiplicand < divisor, "Fractional integer time scaling requires a multiplicand smaller than its divisor.");

			if (multiplicand == 0 || multiplier == 0)
			{
				return 0;
			}

			if (multiplicand <= rox::core::NumericLimits<core::uint64>::max() / multiplier)
			{
				return multiplicand * multiplier / divisor;
			}

			core::uint64 quotient = 0;
			core::uint64 remainder = 0;
			core::uint64 factorQuotient = 0;
			core::uint64 factorRemainder = multiplicand;

			while (multiplier > 0)
			{
				if ((multiplier & 1u) != 0)
				{
					addFraction(quotient, remainder, factorQuotient, factorRemainder, divisor);
				}

				multiplier >>= 1u;

				if (multiplier == 0)
				{
					break;
				}

				const core::uint64 previousFactorQuotient = factorQuotient;
				const core::uint64 previousFactorRemainder = factorRemainder;
				addFraction(factorQuotient, factorRemainder, previousFactorQuotient, previousFactorRemainder, divisor);
			}

			return quotient;
		}

		core::uint64 ticksToNanoseconds(core::uint64 ticks, core::uint64 frequency) noexcept
		{
			ASSERT(frequency > 0, "Performance-counter conversion requires a non-zero frequency.");

			const core::uint64 seconds = ticks / frequency;
			const core::uint64 remainingTicks = ticks % frequency;
			ASSERT(seconds <= rox::core::NumericLimits<core::uint64>::max() / NANOSECONDS_PER_SECOND, "A monotonic time value must fit in uint64 nanoseconds.");

			const core::uint64 wholeNanoseconds = seconds * NANOSECONDS_PER_SECOND;
			const core::uint64 fractionalNanoseconds = multiplyDivideFloor(remainingTicks, NANOSECONDS_PER_SECOND, frequency);
			ASSERT(wholeNanoseconds <= rox::core::NumericLimits<core::uint64>::max() - fractionalNanoseconds, "A monotonic time value must fit in uint64 nanoseconds.");
			return wholeNanoseconds + fractionalNanoseconds;
		}

		core::int64 fileTimeToUnixMicroseconds(const FILETIME& fileTime) noexcept
		{
			ULARGE_INTEGER windowsTime = {};
			windowsTime.LowPart = fileTime.dwLowDateTime;
			windowsTime.HighPart = fileTime.dwHighDateTime;

			if (windowsTime.QuadPart >= WINDOWS_TICKS_AT_UNIX_EPOCH)
			{
				const core::uint64 elapsedTicks = windowsTime.QuadPart - WINDOWS_TICKS_AT_UNIX_EPOCH;
				const core::uint64 elapsedMicroseconds = elapsedTicks / WINDOWS_TICKS_PER_MICROSECOND;
				ASSERT(elapsedMicroseconds <= static_cast<core::uint64>(rox::core::NumericLimits<core::int64>::max()), "A UTC time value must fit in signed Unix microseconds.");
				return static_cast<core::int64>(elapsedMicroseconds);
			}

			const core::uint64 elapsedTicks = WINDOWS_TICKS_AT_UNIX_EPOCH - windowsTime.QuadPart;
			const core::uint64 elapsedMicroseconds = elapsedTicks / WINDOWS_TICKS_PER_MICROSECOND;
			ASSERT(elapsedMicroseconds <= static_cast<core::uint64>(rox::core::NumericLimits<core::int64>::max()), "A UTC time value must fit in signed Unix microseconds.");
			return -static_cast<core::int64>(elapsedMicroseconds);
		}
	} // namespace

	core::Result<WindowsClock> WindowsClock::Create()
	{
		LARGE_INTEGER frequency = {};

		if (QueryPerformanceFrequency(&frequency) == FALSE || frequency.QuadPart <= 0)
		{
			LOG_ERROR("Platform.Clock", "Windows did not provide a valid performance-counter frequency.");

			return core::Result<WindowsClock>::Failure(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-CLOCK-FREQUENCY",
				"Windows did not provide a valid high-resolution performance-counter frequency.",
				"Run the engine on a supported Windows system and retry startup.");
		}

		LARGE_INTEGER origin = {};

		if (QueryPerformanceCounter(&origin) == FALSE)
		{
			LOG_ERROR("Platform.Clock", "Windows did not provide an initial performance-counter value.");

			return core::Result<WindowsClock>::Failure(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-CLOCK-ORIGIN",
				"Windows did not provide an initial high-resolution performance-counter value.",
				"Run the engine on a supported Windows system and retry startup.");
		}

		LOG_INFO("Platform.Clock", "The Windows clock capability was initialized.");

		return core::Result<WindowsClock>::Success(WindowsClock(frequency.QuadPart, origin.QuadPart));
	}

	WindowsClock::WindowsClock(core::int64 frequency, core::int64 originTicks) noexcept
		: m_Frequency(frequency)
		, m_OriginTicks(originTicks)
	{
		ASSERT(m_Frequency > 0, "WindowsClock requires a positive performance-counter frequency.");
	}

	MonotonicTime WindowsClock::NowMonotonic() const noexcept
	{
		LARGE_INTEGER current = {};
		const BOOL bSucceeded = QueryPerformanceCounter(&current);
		ASSERT(bSucceeded != FALSE, "QueryPerformanceCounter must succeed after WindowsClock initialization on a supported system.");
		ASSERT(current.QuadPart >= m_OriginTicks, "The Windows performance counter must not move backwards during a clock lifetime.");

		const core::uint64 elapsedTicks = static_cast<core::uint64>(current.QuadPart) - static_cast<core::uint64>(m_OriginTicks);

		return { ticksToNanoseconds(elapsedTicks, static_cast<core::uint64>(m_Frequency)) };
	}

	UtcTime WindowsClock::NowUtc() const noexcept
	{
		FILETIME fileTime = {};
		GetSystemTimePreciseAsFileTime(&fileTime);

		return { fileTimeToUnixMicroseconds(fileTime) };
	}
} // namespace rox::os
