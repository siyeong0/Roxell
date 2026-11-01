#include <Int2.hpp>
#include <Numeric.hpp>
#include <Comparison.hpp>
#include "IntegerMathInternal.hpp"
#include <Assertion.h>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	Int2 Int2::operator-() const noexcept
	{
		return {detail::NarrowIntegerLane(-static_cast<int64>(X)), detail::NarrowIntegerLane(-static_cast<int64>(Y))};
	}

	Int2 Int2::operator+(const Int2& other) const noexcept
	{
		return {detail::NarrowIntegerLane(static_cast<int64>(X) + other.X), detail::NarrowIntegerLane(static_cast<int64>(Y) + other.Y)};
	}

	Int2 Int2::operator-(const Int2& other) const noexcept
	{
		return {detail::NarrowIntegerLane(static_cast<int64>(X) - other.X), detail::NarrowIntegerLane(static_cast<int64>(Y) - other.Y)};
	}

	Int2 Int2::operator*(const Int2& other) const noexcept
	{
		return {detail::NarrowIntegerLane(static_cast<int64>(X) * other.X), detail::NarrowIntegerLane(static_cast<int64>(Y) * other.Y)};
	}

	Int2 Int2::operator/(const Int2& other) const noexcept
	{
		ASSERT(other.X != 0 && other.Y != 0, "Integer vector divisors must be nonzero.");
		return {detail::NarrowIntegerLane(static_cast<int64>(X) / other.X), detail::NarrowIntegerLane(static_cast<int64>(Y) / other.Y)};
	}

	Int2 Int2::operator*(const int32 scalar) const noexcept
	{
		return *this * Int2(scalar, scalar);
	}

	Int2 Int2::operator/(const int32 scalar) const noexcept
	{
		ASSERT(scalar != 0, "Integer vector divisors must be nonzero.");
		return *this / Int2(scalar, scalar);
	}

	Int2 operator*(const int32 scalar, const Int2& value) noexcept
	{
		return value * scalar;
	}

	Int2& Int2::operator+=(const Int2& other) noexcept
	{
		return *this = *this + other;
	}

	Int2& Int2::operator-=(const Int2& other) noexcept
	{
		return *this = *this - other;
	}

	Int2& Int2::operator*=(const Int2& other) noexcept
	{
		return *this = *this * other;
	}

	Int2& Int2::operator/=(const Int2& other) noexcept
	{
		return *this = *this / other;
	}

	Int2& Int2::operator*=(const int32 scalar) noexcept
	{
		return *this = *this * scalar;
	}

	Int2& Int2::operator/=(const int32 scalar) noexcept
	{
		return *this = *this / scalar;
	}

	Int2 Int2::Min(const Int2& other) const noexcept
	{
		return {core::Min(X, other.X), core::Min(Y, other.Y)};
	}

	Int2 Int2::Max(const Int2& other) const noexcept
	{
		return {core::Max(X, other.X), core::Max(Y, other.Y)};
	}

	Int2 Int2::Abs() const noexcept
	{
		return {detail::NarrowIntegerLane(core::Abs(static_cast<int64>(X))),
		        detail::NarrowIntegerLane(core::Abs(static_cast<int64>(Y)))};
	}

	Int2 Int2::Clamped(const Int2& minimum, const Int2& maximum) const noexcept
	{
		ASSERT(minimum.X <= maximum.X && minimum.Y <= maximum.Y, "Integer vector clamp bounds must be ordered.");
		return {core::ClampValue(X, minimum.X, maximum.X), core::ClampValue(Y, minimum.Y, maximum.Y)};
	}

	template <MathScalar T>
	Int2 Int2::Floor(const Vector2<T>& value) noexcept
	{
		ASSERT(value.IsFinite(), "Integer vector conversion input must be finite.");
		const T roundedX = core::Floor(static_cast<T>(value.X));
		const T roundedY = core::Floor(static_cast<T>(value.Y));
		ASSERT(detail::IsIntegerConversionRepresentable<T>(roundedX) && detail::IsIntegerConversionRepresentable<T>(roundedY), "Integer vector conversion result must be representable as int32.");
		return {static_cast<int32>(roundedX), static_cast<int32>(roundedY)};
	}

	template <MathScalar T>
	Int2 Int2::Ceil(const Vector2<T>& value) noexcept
	{
		ASSERT(value.IsFinite(), "Integer vector conversion input must be finite.");
		const T roundedX = core::Ceil(static_cast<T>(value.X));
		const T roundedY = core::Ceil(static_cast<T>(value.Y));
		ASSERT(detail::IsIntegerConversionRepresentable<T>(roundedX) && detail::IsIntegerConversionRepresentable<T>(roundedY), "Integer vector conversion result must be representable as int32.");
		return {static_cast<int32>(roundedX), static_cast<int32>(roundedY)};
	}

	template <MathScalar T>
	Int2 Int2::Round(const Vector2<T>& value) noexcept
	{
		ASSERT(value.IsFinite(), "Integer vector conversion input must be finite.");
		const T roundedX = core::Round(static_cast<T>(value.X));
		const T roundedY = core::Round(static_cast<T>(value.Y));
		ASSERT(detail::IsIntegerConversionRepresentable<T>(roundedX) && detail::IsIntegerConversionRepresentable<T>(roundedY), "Integer vector conversion result must be representable as int32.");
		return {static_cast<int32>(roundedX), static_cast<int32>(roundedY)};
	}

	template <MathScalar T>
	Vector2<T> Int2::ToVector2() const noexcept
	{
		return {static_cast<T>(X), static_cast<T>(Y)};
	}

	template Int2 Int2::Floor(const Vector2<float32>&) noexcept;
	template Int2 Int2::Ceil(const Vector2<float32>&) noexcept;
	template Int2 Int2::Round(const Vector2<float32>&) noexcept;
	template Vector2<float32> Int2::ToVector2<float32>() const noexcept;
	template Int2 Int2::Floor(const Vector2<float64>&) noexcept;
	template Int2 Int2::Ceil(const Vector2<float64>&) noexcept;
	template Int2 Int2::Round(const Vector2<float64>&) noexcept;
	template Vector2<float64> Int2::ToVector2<float64>() const noexcept;
} // namespace rox::math
