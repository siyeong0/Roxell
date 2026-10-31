#include <Int3.hpp>
#include <Numeric.hpp>
#include <Comparison.hpp>
#include "IntegerMathInternal.hpp"
#include <Assertion.h>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	Int3 Int3::operator-() const noexcept
	{
		return {detail::NarrowIntegerLane(-static_cast<int64>(X)), 
				detail::NarrowIntegerLane(-static_cast<int64>(Y)),
		        detail::NarrowIntegerLane(-static_cast<int64>(Z))};
	}

	Int3 Int3::operator+(const Int3& other) const noexcept
	{
		return {detail::NarrowIntegerLane(static_cast<int64>(X) + other.X), 
				detail::NarrowIntegerLane(static_cast<int64>(Y) + other.Y),
		        detail::NarrowIntegerLane(static_cast<int64>(Z) + other.Z)};
	}

	Int3 Int3::operator-(const Int3& other) const noexcept
	{
		return {detail::NarrowIntegerLane(static_cast<int64>(X) - other.X), 
				detail::NarrowIntegerLane(static_cast<int64>(Y) - other.Y),
		        detail::NarrowIntegerLane(static_cast<int64>(Z) - other.Z)};
	}

	Int3 Int3::operator*(const Int3& other) const noexcept
	{
		return {detail::NarrowIntegerLane(static_cast<int64>(X) * other.X), 
				detail::NarrowIntegerLane(static_cast<int64>(Y) * other.Y),
		        detail::NarrowIntegerLane(static_cast<int64>(Z) * other.Z)};
	}

	Int3 Int3::operator/(const Int3& other) const noexcept
	{
		ASSERT(other.X != 0 && other.Y != 0 && other.Z != 0, "Integer vector divisors must be nonzero.");
		return {detail::NarrowIntegerLane(static_cast<int64>(X) / other.X), 
				detail::NarrowIntegerLane(static_cast<int64>(Y) / other.Y),
		        detail::NarrowIntegerLane(static_cast<int64>(Z) / other.Z)};
	}

	Int3 Int3::operator*(const int32 scalar) const noexcept
	{
		return *this * Int3(scalar, scalar, scalar);
	}

	Int3 Int3::operator/(const int32 scalar) const noexcept
	{
		ASSERT(scalar != 0, "Integer vector divisors must be nonzero.");
		return *this / Int3(scalar, scalar, scalar);
	}

	Int3 operator*(const int32 scalar, const Int3& value) noexcept
	{
		return value * scalar;
	}

	Int3& Int3::operator+=(const Int3& other) noexcept
	{
		return *this = *this + other;
	}

	Int3& Int3::operator-=(const Int3& other) noexcept
	{
		return *this = *this - other;
	}

	Int3& Int3::operator*=(const Int3& other) noexcept
	{
		return *this = *this * other;
	}

	Int3& Int3::operator/=(const Int3& other) noexcept
	{
		return *this = *this / other;
	}

	Int3& Int3::operator*=(const int32 scalar) noexcept
	{
		return *this = *this * scalar;
	}

	Int3& Int3::operator/=(const int32 scalar) noexcept
	{
		return *this = *this / scalar;
	}

	Int3 Int3::Min(const Int3& other) const noexcept
	{
		return {rox::core::Min(X, other.X), rox::core::Min(Y, other.Y), rox::core::Min(Z, other.Z)};
	}

	Int3 Int3::Max(const Int3& other) const noexcept
	{
		return {rox::core::Max(X, other.X), rox::core::Max(Y, other.Y), rox::core::Max(Z, other.Z)};
	}

	Int3 Int3::Abs() const noexcept
	{
		return {detail::NarrowIntegerLane(rox::core::Abs(static_cast<int64>(X))),
		        detail::NarrowIntegerLane(rox::core::Abs(static_cast<int64>(Y))),
		        detail::NarrowIntegerLane(rox::core::Abs(static_cast<int64>(Z)))};
	}

	Int3 Int3::Clamped(const Int3& minimum, const Int3& maximum) const noexcept
	{
		ASSERT(minimum.X <= maximum.X && minimum.Y <= maximum.Y && minimum.Z <= maximum.Z, "Integer vector clamp bounds must be ordered.");
		return {rox::core::ClampValue(X, minimum.X, maximum.X), 
				rox::core::ClampValue(Y, minimum.Y, maximum.Y),
		        rox::core::ClampValue(Z, minimum.Z, maximum.Z)};
	}

	template <MathScalar T>
	Int3 Int3::Floor(const Vector3<T>& value) noexcept
	{
		ASSERT(value.IsFinite(), "Integer vector conversion input must be finite.");
		const T roundedX = rox::core::Floor(static_cast<T>(value.X));
		const T roundedY = rox::core::Floor(static_cast<T>(value.Y));
		const T roundedZ = rox::core::Floor(static_cast<T>(value.Z));
		ASSERT(detail::IsIntegerConversionRepresentable<T>(roundedX) && detail::IsIntegerConversionRepresentable<T>(roundedY) && detail::IsIntegerConversionRepresentable<T>(roundedZ),
		       "Integer vector conversion result must be representable as int32.");
		return {static_cast<int32>(roundedX), static_cast<int32>(roundedY), static_cast<int32>(roundedZ)};
	}

	template <MathScalar T>
	Int3 Int3::Ceil(const Vector3<T>& value) noexcept
	{
		ASSERT(value.IsFinite(), "Integer vector conversion input must be finite.");
		const T roundedX = rox::core::Ceil(static_cast<T>(value.X));
		const T roundedY = rox::core::Ceil(static_cast<T>(value.Y));
		const T roundedZ = rox::core::Ceil(static_cast<T>(value.Z));
		ASSERT(detail::IsIntegerConversionRepresentable<T>(roundedX) && detail::IsIntegerConversionRepresentable<T>(roundedY) && detail::IsIntegerConversionRepresentable<T>(roundedZ),
		       "Integer vector conversion result must be representable as int32.");
		return {static_cast<int32>(roundedX), static_cast<int32>(roundedY), static_cast<int32>(roundedZ)};
	}

	template <MathScalar T>
	Int3 Int3::Round(const Vector3<T>& value) noexcept
	{
		ASSERT(value.IsFinite(), "Integer vector conversion input must be finite.");
		const T roundedX = rox::core::Round(static_cast<T>(value.X));
		const T roundedY = rox::core::Round(static_cast<T>(value.Y));
		const T roundedZ = rox::core::Round(static_cast<T>(value.Z));
		ASSERT(detail::IsIntegerConversionRepresentable<T>(roundedX) && detail::IsIntegerConversionRepresentable<T>(roundedY) && detail::IsIntegerConversionRepresentable<T>(roundedZ),
		       "Integer vector conversion result must be representable as int32.");
		return {static_cast<int32>(roundedX), static_cast<int32>(roundedY), static_cast<int32>(roundedZ)};
	}

	template <MathScalar T>
	Vector3<T> Int3::ToVector3() const noexcept
	{
		return {static_cast<T>(X), static_cast<T>(Y), static_cast<T>(Z)};
	}

	template Int3 Int3::Floor(const Vector3<float32>&) noexcept;
	template Int3 Int3::Ceil(const Vector3<float32>&) noexcept;
	template Int3 Int3::Round(const Vector3<float32>&) noexcept;
	template Vector3<float32> Int3::ToVector3<float32>() const noexcept;
	template Int3 Int3::Floor(const Vector3<float64>&) noexcept;
	template Int3 Int3::Ceil(const Vector3<float64>&) noexcept;
	template Int3 Int3::Round(const Vector3<float64>&) noexcept;
	template Vector3<float64> Int3::ToVector3<float64>() const noexcept;
} // namespace rox::math
