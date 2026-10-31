#pragma once

#include <Assertion.h>
#include <ClockPrimitives.hpp>
#include <Types.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>

namespace rox::core
{
	class Duration final
	{
	public:
		constexpr Duration() noexcept = default;

		static constexpr Duration FromNanoseconds(rox::core::int64 value) noexcept { return Duration(value); }
		static constexpr Duration FromMicroseconds(rox::core::int64 value) noexcept { return Duration(value) * 1000; }
		static constexpr Duration FromMilliseconds(rox::core::int64 value) noexcept { return Duration(value) * 1000000; }

		static Duration FromSeconds(double value) noexcept
		{
			const double nanoseconds = value * 1000000000.0;
			ASSERT(rox::core::IsFinite(nanoseconds) && nanoseconds >= -0x1p63 && nanoseconds < 0x1p63, "Duration seconds must scale to finite representable nanoseconds.");
			return Duration(static_cast<rox::core::int64>(nanoseconds));
		}

		static constexpr Duration FromChrono(rox::core::Nanoseconds value) noexcept
		{
			return Duration(value.count());
		}

		constexpr rox::core::Nanoseconds ToChrono() const noexcept { return rox::core::Nanoseconds(m_Nanoseconds); }
		constexpr rox::core::int64 ToNanoseconds() const noexcept { return m_Nanoseconds; }
		constexpr double ToMicroseconds() const noexcept { return static_cast<double>(m_Nanoseconds) / 1000.0; }
		constexpr double ToMilliseconds() const noexcept { return static_cast<double>(m_Nanoseconds) / 1000000.0; }
		constexpr double ToSeconds() const noexcept { return static_cast<double>(m_Nanoseconds) / 1000000000.0; }

		constexpr auto operator<=>(const Duration&) const noexcept = default;

		constexpr Duration operator+(Duration right) const noexcept
		{
			ASSERT(right.m_Nanoseconds > 0 ? m_Nanoseconds <= maximum() - right.m_Nanoseconds
			                               : m_Nanoseconds >= minimum() - right.m_Nanoseconds,
			       "Duration addition must remain representable.");
			return Duration(m_Nanoseconds + right.m_Nanoseconds);
		}

		constexpr Duration operator-(Duration right) const noexcept
		{
			ASSERT(right.m_Nanoseconds > 0 ? m_Nanoseconds >= minimum() + right.m_Nanoseconds
			                               : m_Nanoseconds <= maximum() + right.m_Nanoseconds,
			       "Duration subtraction must remain representable.");
			return Duration(m_Nanoseconds - right.m_Nanoseconds);
		}

		constexpr Duration operator-() const noexcept
		{
			ASSERT(m_Nanoseconds != minimum(), "Duration negation must remain representable.");
			return Duration(-m_Nanoseconds);
		}

		constexpr Duration operator*(rox::core::int64 factor) const noexcept
		{
			const auto count = m_Nanoseconds;

			ASSERT(count == 0 || factor == 0 ||
			           (count > 0 && factor >= minimum() / count && factor <= maximum() / count) ||
			           (count < 0 && factor > 0 && count >= minimum() / factor) ||
			           (count < 0 && factor < 0 && count >= maximum() / factor),
			       "Duration multiplication must remain representable.");

			return Duration(count * factor);
		}

		constexpr Duration operator/(rox::core::int64 divisor) const noexcept
		{
			ASSERT(divisor != 0 && !(m_Nanoseconds == minimum() && divisor == -1),
			       "Duration division requires a nonzero divisor and representable result.");
			return Duration(m_Nanoseconds / divisor);
		}

		constexpr double operator/(Duration divisor) const noexcept
		{
			ASSERT(divisor.m_Nanoseconds != 0, "Duration ratio requires a nonzero duration.");
			return static_cast<double>(m_Nanoseconds) / static_cast<double>(divisor.m_Nanoseconds);
		}

		constexpr Duration& operator+=(Duration other) noexcept { return *this = *this + other; }
		constexpr Duration& operator-=(Duration other) noexcept { return *this = *this - other; }
		constexpr Duration& operator*=(rox::core::int64 factor) noexcept { return *this = *this * factor; }
		constexpr Duration& operator/=(rox::core::int64 divisor) noexcept { return *this = *this / divisor; }

	private:
		explicit constexpr Duration(rox::core::int64 value) noexcept
		    : m_Nanoseconds(value)
		{
		}

		static constexpr rox::core::int64 minimum() noexcept { return rox::core::NumericLimits<rox::core::int64>::min(); }
		static constexpr rox::core::int64 maximum() noexcept { return rox::core::NumericLimits<rox::core::int64>::max(); }

	private:
		rox::core::int64 m_Nanoseconds = 0;
	};

	constexpr Duration operator*(rox::core::int64 factor, Duration value) noexcept { return value * factor; }
}
