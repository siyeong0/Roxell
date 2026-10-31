#include <Int4.hpp>
#include <Numeric.hpp>
#include <Comparison.hpp>
#include "IntegerMathInternal.hpp"
#include <Assertion.h>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	Int4 Int4::operator-() const noexcept
	{
		return {detail::NarrowIntegerLane(-static_cast<int64>(X)),
				detail::NarrowIntegerLane(-static_cast<int64>(Y)),
		        detail::NarrowIntegerLane(-static_cast<int64>(Z)), 
				detail::NarrowIntegerLane(-static_cast<int64>(W))};
	}

	Int4 Int4::operator+(const Int4& other) const noexcept
	{
		return {detail::NarrowIntegerLane(static_cast<int64>(X) + other.X), 
				detail::NarrowIntegerLane(static_cast<int64>(Y) + other.Y),
		        detail::NarrowIntegerLane(static_cast<int64>(Z) + other.Z), 
				detail::NarrowIntegerLane(static_cast<int64>(W) + other.W)};
	}

	Int4 Int4::operator-(const Int4& other) const noexcept
	{
		return {detail::NarrowIntegerLane(static_cast<int64>(X) - other.X), 
				detail::NarrowIntegerLane(static_cast<int64>(Y) - other.Y),
		        detail::NarrowIntegerLane(static_cast<int64>(Z) - other.Z), 
				detail::NarrowIntegerLane(static_cast<int64>(W) - other.W)};
	}

	Int4 Int4::operator*(const Int4& other) const noexcept
	{
		return {detail::NarrowIntegerLane(static_cast<int64>(X) * other.X), 
				detail::NarrowIntegerLane(static_cast<int64>(Y) * other.Y),
		        detail::NarrowIntegerLane(static_cast<int64>(Z) * other.Z), 
				detail::NarrowIntegerLane(static_cast<int64>(W) * other.W)};
	}

	Int4 Int4::operator/(const Int4& other) const noexcept
	{
		ASSERT(other.X != 0 && other.Y != 0 && other.Z != 0 && other.W != 0, "Integer vector divisors must be nonzero.");
		return {detail::NarrowIntegerLane(static_cast<int64>(X) / other.X), 
				detail::NarrowIntegerLane(static_cast<int64>(Y) / other.Y),
		        detail::NarrowIntegerLane(static_cast<int64>(Z) / other.Z), 
				detail::NarrowIntegerLane(static_cast<int64>(W) / other.W)};
	}

	Int4 Int4::operator*(const int32 scalar) const noexcept
	{
		return *this * Int4(scalar, scalar, scalar, scalar);
	}

	Int4 Int4::operator/(const int32 scalar) const noexcept
	{
		ASSERT(scalar != 0, "Integer vector divisors must be nonzero.");
		return *this / Int4(scalar, scalar, scalar, scalar);
	}

	Int4 operator*(const int32 scalar, const Int4& value) noexcept
	{
		return value * scalar;
	}

	Int4& Int4::operator+=(const Int4& other) noexcept
	{
		return *this = *this + other;
	}

	Int4& Int4::operator-=(const Int4& other) noexcept
	{
		return *this = *this - other;
	}

	Int4& Int4::operator*=(const Int4& other) noexcept
	{
		return *this = *this * other;
	}

	Int4& Int4::operator/=(const Int4& other) noexcept
	{
		return *this = *this / other;
	}

	Int4& Int4::operator*=(const int32 scalar) noexcept
	{
		return *this = *this * scalar;
	}

	Int4& Int4::operator/=(const int32 scalar) noexcept
	{
		return *this = *this / scalar;
	}

	Int4 Int4::Min(const Int4& other) const noexcept
	{
		return {rox::core::Min(X, other.X), 
				rox::core::Min(Y, other.Y), 
				rox::core::Min(Z, other.Z),
		        rox::core::Min(W, other.W)};
	}

	Int4 Int4::Max(const Int4& other) const noexcept
	{
		return {rox::core::Max(X, other.X), 
				rox::core::Max(Y, other.Y), 
				rox::core::Max(Z, other.Z),
		        rox::core::Max(W, other.W)};
	}

	Int4 Int4::Abs() const noexcept
	{
		return {detail::NarrowIntegerLane(rox::core::Abs(static_cast<int64>(X))),
		        detail::NarrowIntegerLane(rox::core::Abs(static_cast<int64>(Y))),
		        detail::NarrowIntegerLane(rox::core::Abs(static_cast<int64>(Z))),
		        detail::NarrowIntegerLane(rox::core::Abs(static_cast<int64>(W)))};
	}

	Int4 Int4::Clamped(const Int4& minimum, const Int4& maximum) const noexcept
	{
		ASSERT(minimum.X <= maximum.X && minimum.Y <= maximum.Y && minimum.Z <= maximum.Z && minimum.W <= maximum.W, "Integer vector clamp bounds must be ordered.");
		return {rox::core::ClampValue(X, minimum.X, maximum.X), 
				rox::core::ClampValue(Y, minimum.Y, maximum.Y),
		        rox::core::ClampValue(Z, minimum.Z, maximum.Z), 
				rox::core::ClampValue(W, minimum.W, maximum.W)};
	}

	template <MathScalar T>
	Int4 Int4::Floor(const Vector4<T>& value) noexcept
	{
		ASSERT(value.IsFinite(), "Integer vector conversion input must be finite.");
		const T roundedX = rox::core::Floor(static_cast<T>(value.X));
		const T roundedY = rox::core::Floor(static_cast<T>(value.Y));
		const T roundedZ = rox::core::Floor(static_cast<T>(value.Z));
		const T roundedW = rox::core::Floor(static_cast<T>(value.W));
		ASSERT(detail::IsIntegerConversionRepresentable<T>(roundedX) && detail::IsIntegerConversionRepresentable<T>(roundedY) && detail::IsIntegerConversionRepresentable<T>(roundedZ) && detail::IsIntegerConversionRepresentable<T>(roundedW),
		       "Integer vector conversion result must be representable as int32.");
		return {static_cast<int32>(roundedX), 
				static_cast<int32>(roundedY), 
				static_cast<int32>(roundedZ),
		        static_cast<int32>(roundedW)};
	}

	template <MathScalar T>
	Int4 Int4::Ceil(const Vector4<T>& value) noexcept
	{
		ASSERT(value.IsFinite(), "Integer vector conversion input must be finite.");
		const T roundedX = rox::core::Ceil(static_cast<T>(value.X));
		const T roundedY = rox::core::Ceil(static_cast<T>(value.Y));
		const T roundedZ = rox::core::Ceil(static_cast<T>(value.Z));
		const T roundedW = rox::core::Ceil(static_cast<T>(value.W));
		ASSERT(detail::IsIntegerConversionRepresentable<T>(roundedX) && detail::IsIntegerConversionRepresentable<T>(roundedY) && detail::IsIntegerConversionRepresentable<T>(roundedZ) && detail::IsIntegerConversionRepresentable<T>(roundedW),
		       "Integer vector conversion result must be representable as int32.");
		return {static_cast<int32>(roundedX), 
				static_cast<int32>(roundedY), 
				static_cast<int32>(roundedZ),
		        static_cast<int32>(roundedW)};
	}

	template <MathScalar T>
	Int4 Int4::Round(const Vector4<T>& value) noexcept
	{
		ASSERT(value.IsFinite(), "Integer vector conversion input must be finite.");
		const T roundedX = rox::core::Round(static_cast<T>(value.X));
		const T roundedY = rox::core::Round(static_cast<T>(value.Y));
		const T roundedZ = rox::core::Round(static_cast<T>(value.Z));
		const T roundedW = rox::core::Round(static_cast<T>(value.W));
		ASSERT(detail::IsIntegerConversionRepresentable<T>(roundedX) && detail::IsIntegerConversionRepresentable<T>(roundedY) && detail::IsIntegerConversionRepresentable<T>(roundedZ) && detail::IsIntegerConversionRepresentable<T>(roundedW),
		       "Integer vector conversion result must be representable as int32.");
		return {static_cast<int32>(roundedX), 
				static_cast<int32>(roundedY), 
				static_cast<int32>(roundedZ),
		        static_cast<int32>(roundedW)};
	}

	template <MathScalar T>
	Vector4<T> Int4::ToVector4() const noexcept
	{
		return {static_cast<T>(X), static_cast<T>(Y), static_cast<T>(Z), static_cast<T>(W)};
	}

	template Int4 Int4::Floor(const Vector4<float32>&) noexcept;
	template Int4 Int4::Ceil(const Vector4<float32>&) noexcept;
	template Int4 Int4::Round(const Vector4<float32>&) noexcept;
	template Vector4<float32> Int4::ToVector4<float32>() const noexcept;
	template Int4 Int4::Floor(const Vector4<float64>&) noexcept;
	template Int4 Int4::Ceil(const Vector4<float64>&) noexcept;
	template Int4 Int4::Round(const Vector4<float64>&) noexcept;
	template Vector4<float64> Int4::ToVector4<float64>() const noexcept;
} // namespace rox::math
