#include <WindowsClock.h>
#include <NumericLimits.hpp>
#include <Assertion.h>
#include <Log.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace rox::os
{
	using namespace core;

	namespace
	{
		constexpr uint64 NANOSECONDS_PER_SECOND = 1'000'000'000;
		constexpr uint64 WINDOWS_TICKS_PER_MICROSECOND = 10;
		constexpr uint64 WINDOWS_TICKS_AT_UNIX_EPOCH = 116'444'736'000'000'000;

		void addFraction(uint64& quotient, uint64& remainder, uint64 addedQuotient, uint64 addedRemainder,
			uint64 divisor) noexcept
		{
			ASSERT(remainder < divisor, "A scaled-time remainder must be smaller than its divisor.");
			ASSERT(addedRemainder < divisor, "An added scaled-time remainder must be smaller than its divisor.");
			ASSERT(quotient <= NumericLimits<uint64>::max() - addedQuotient, "A scaled-time quotient must fit in uint64.");

			quotient += addedQuotient;

			if (remainder >= divisor - addedRemainder)
			{
				remainder -= divisor - addedRemainder;
				ASSERT(quotient < NumericLimits<uint64>::max(), "A scaled-time carry must fit in uint64.");
				++quotient;

				return;
			}

			remainder += addedRemainder;
		}

		uint64 multiplyDivideFloor(uint64 multiplicand, uint64 multiplier, uint64 divisor) noexcept
		{
			ASSERT(divisor > 0, "Integer time scaling requires a non-zero divisor.");
			ASSERT(multiplicand < divisor, "Fractional integer time scaling requires a multiplicand smaller than its divisor.");

			if (multiplicand == 0 || multiplier == 0)
			{
				return 0;
			}

			if (multiplicand <= NumericLimits<uint64>::max() / multiplier)
			{
				return multiplicand * multiplier / divisor;
			}

			uint64 quotient = 0;
			uint64 remainder = 0;
			uint64 factorQuotient = 0;
			uint64 factorRemainder = multiplicand;

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

				const uint64 previousFactorQuotient = factorQuotient;
				const uint64 previousFactorRemainder = factorRemainder;
				addFraction(factorQuotient, factorRemainder, previousFactorQuotient, previousFactorRemainder, divisor);
			}

			return quotient;
		}

		uint64 ticksToNanoseconds(uint64 ticks, uint64 frequency) noexcept
		{
			ASSERT(frequency > 0, "Performance-counter conversion requires a non-zero frequency.");

			const uint64 seconds = ticks / frequency;
			const uint64 remainingTicks = ticks % frequency;
			ASSERT(seconds <= NumericLimits<uint64>::max() / NANOSECONDS_PER_SECOND, "A monotonic time value must fit in uint64 nanoseconds.");

			const uint64 wholeNanoseconds = seconds * NANOSECONDS_PER_SECOND;
			const uint64 fractionalNanoseconds = multiplyDivideFloor(remainingTicks, NANOSECONDS_PER_SECOND, frequency);
			ASSERT(wholeNanoseconds <= NumericLimits<uint64>::max() - fractionalNanoseconds, "A monotonic time value must fit in uint64 nanoseconds.");
			return wholeNanoseconds + fractionalNanoseconds;
		}

		int64 fileTimeToUnixMicroseconds(const FILETIME& fileTime) noexcept
		{
			ULARGE_INTEGER windowsTime = {};
			windowsTime.LowPart = fileTime.dwLowDateTime;
			windowsTime.HighPart = fileTime.dwHighDateTime;

			if (windowsTime.QuadPart >= WINDOWS_TICKS_AT_UNIX_EPOCH)
			{
				const uint64 elapsedTicks = windowsTime.QuadPart - WINDOWS_TICKS_AT_UNIX_EPOCH;
				const uint64 elapsedMicroseconds = elapsedTicks / WINDOWS_TICKS_PER_MICROSECOND;
				ASSERT(elapsedMicroseconds <= static_cast<uint64>(NumericLimits<int64>::max()), "A UTC time value must fit in signed Unix microseconds.");
				return static_cast<int64>(elapsedMicroseconds);
			}

			const uint64 elapsedTicks = WINDOWS_TICKS_AT_UNIX_EPOCH - windowsTime.QuadPart;
			const uint64 elapsedMicroseconds = elapsedTicks / WINDOWS_TICKS_PER_MICROSECOND;
			ASSERT(elapsedMicroseconds <= static_cast<uint64>(NumericLimits<int64>::max()), "A UTC time value must fit in signed Unix microseconds.");
			return -static_cast<int64>(elapsedMicroseconds);
		}
	} // namespace

	Result<WindowsClock> WindowsClock::Create()
	{
		LARGE_INTEGER frequency = {};

		if (QueryPerformanceFrequency(&frequency) == FALSE || frequency.QuadPart <= 0)
		{
			LOG_ERROR("Platform.Clock", "Windows did not provide a valid performance-counter frequency.");

			return Result<WindowsClock>::Failure(EErrorCode::ExternalFailure, "ROX-PLATFORM-CLOCK-FREQUENCY",
				"Windows did not provide a valid high-resolution performance-counter frequency.",
				"Run the engine on a supported Windows system and retry startup.");
		}

		LARGE_INTEGER origin = {};

		if (QueryPerformanceCounter(&origin) == FALSE)
		{
			LOG_ERROR("Platform.Clock", "Windows did not provide an initial performance-counter value.");

			return Result<WindowsClock>::Failure(EErrorCode::ExternalFailure, "ROX-PLATFORM-CLOCK-ORIGIN",
				"Windows did not provide an initial high-resolution performance-counter value.",
				"Run the engine on a supported Windows system and retry startup.");
		}

		LOG_INFO("Platform.Clock", "The Windows clock capability was initialized.");

		return Result<WindowsClock>::Success(WindowsClock(frequency.QuadPart, origin.QuadPart));
	}

	WindowsClock::WindowsClock(int64 frequency, int64 originTicks) noexcept
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

		const uint64 elapsedTicks = static_cast<uint64>(current.QuadPart) - static_cast<uint64>(m_OriginTicks);

		return { ticksToNanoseconds(elapsedTicks, static_cast<uint64>(m_Frequency)) };
	}

	UtcTime WindowsClock::NowUtc() const noexcept
	{
		FILETIME fileTime = {};
		GetSystemTimePreciseAsFileTime(&fileTime);

		return { fileTimeToUnixMicroseconds(fileTime) };
	}
} // namespace rox::os
