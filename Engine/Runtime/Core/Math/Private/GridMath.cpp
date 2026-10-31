#include <GridMath.h>
#include "IntegerMathInternal.hpp"
#include <Assertion.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>

#include <cmath>
#include <limits>

namespace rox::math
{
	int32 FloorDiv(const int32 value, const int32 divisor) noexcept
	{
		ASSERT(divisor > 0, "Grid divisor must be positive.");
		const int32 quotient = value / divisor;
		return quotient - (value % divisor < 0 ? 1 : 0);
	}

	int32 FloorMod(const int32 value, const int32 divisor) noexcept
	{
		ASSERT(divisor > 0, "Grid divisor must be positive.");
		const int32 remainder = value % divisor;
		return remainder < 0 ? remainder + divisor : remainder;
	}

	uint32 DivideRoundUp(const uint32 value, const uint32 divisor) noexcept
	{
		ASSERT(divisor > 0, "Grid divisor must be positive.");
		return value / divisor + (value % divisor != 0 ? 1u : 0u);
	}

	template <MathScalar T>
	static T cellCoordinate(T point, T origin, T size) noexcept
	{
		const T difference = point - origin;
		return rox::core::Floor(rox::core::IsFinite(difference) ? difference / size : point / size - origin / size);
	}

	template <MathScalar T>
	Int2 PointToCell(const Vector2<T>& point, const Vector2<T>& origin, const Vector2<T>& cellSize) noexcept
	{
		ASSERT(point.IsFinite() && origin.IsFinite() && cellSize.IsFinite() && cellSize.X > 0 && cellSize.Y > 0, "Grid inputs must be finite with positive cell sizes.");
		const T x = cellCoordinate(point.X, origin.X, cellSize.X);
		const T y = cellCoordinate(point.Y, origin.Y, cellSize.Y);
		ASSERT(detail::IsIntegerConversionRepresentable<T>(x) && detail::IsIntegerConversionRepresentable<T>(y), "Grid cell must be representable as int32.");
		return {static_cast<int32>(x), static_cast<int32>(y)};
	}

	template <MathScalar T>
	Int3 PointToCell(const Vector3<T>& point, const Vector3<T>& origin, const Vector3<T>& cellSize) noexcept
	{
		ASSERT(point.IsFinite() && origin.IsFinite() && cellSize.IsFinite() && cellSize.X > 0 && cellSize.Y > 0 && cellSize.Z > 0, "Grid inputs must be finite with positive cell sizes.");
		const T x = cellCoordinate(point.X, origin.X, cellSize.X);
		const T y = cellCoordinate(point.Y, origin.Y, cellSize.Y);
		const T z = cellCoordinate(point.Z, origin.Z, cellSize.Z);
		ASSERT(detail::IsIntegerConversionRepresentable<T>(x) && detail::IsIntegerConversionRepresentable<T>(y) && detail::IsIntegerConversionRepresentable<T>(z), "Grid cell must be representable as int32.");
		return {static_cast<int32>(x), static_cast<int32>(y), static_cast<int32>(z)};
	}

	template <MathScalar T>
	Vector2<T> CellMinimum(const Int2& cell, const Vector2<T>& origin, const Vector2<T>& cellSize) noexcept
	{
		ASSERT(origin.IsFinite() && cellSize.IsFinite() && cellSize.X > 0 && cellSize.Y > 0, "Grid inputs must be finite with positive cell sizes.");
		const T x = rox::core::Fma(static_cast<T>(cell.X), cellSize.X, origin.X);
		const T y = rox::core::Fma(static_cast<T>(cell.Y), cellSize.Y, origin.Y);
		constexpr T MAXIMUM = rox::core::NumericLimits<T>::max();
		ASSERT(rox::core::Abs(x) <= MAXIMUM && rox::core::Abs(y) <= MAXIMUM, "Grid point must be representable in the finite scalar range.");
		return {static_cast<T>(x), static_cast<T>(y)};
	}

	template <MathScalar T>
	Vector3<T> CellMinimum(const Int3& cell, const Vector3<T>& origin, const Vector3<T>& cellSize) noexcept
	{
		ASSERT(origin.IsFinite() && cellSize.IsFinite() && cellSize.X > 0 && cellSize.Y > 0 && cellSize.Z > 0, "Grid inputs must be finite with positive cell sizes.");
		const T x = rox::core::Fma(static_cast<T>(cell.X), cellSize.X, origin.X);
		const T y = rox::core::Fma(static_cast<T>(cell.Y), cellSize.Y, origin.Y);
		const T z = rox::core::Fma(static_cast<T>(cell.Z), cellSize.Z, origin.Z);
		constexpr T MAXIMUM = rox::core::NumericLimits<T>::max();
		ASSERT(rox::core::Abs(x) <= MAXIMUM && rox::core::Abs(y) <= MAXIMUM && rox::core::Abs(z) <= MAXIMUM, "Grid point must be representable in the finite scalar range.");
		return {static_cast<T>(x), static_cast<T>(y), static_cast<T>(z)};
	}

	template Int2 PointToCell(const Vector2<float32>& point, const Vector2<float32>& origin, const Vector2<float32>& cellSize) noexcept;
	template Int2 PointToCell(const Vector2<float64>& point, const Vector2<float64>& origin, const Vector2<float64>& cellSize) noexcept;
	template Int3 PointToCell(const Vector3<float32>& point, const Vector3<float32>& origin, const Vector3<float32>& cellSize) noexcept;
	template Int3 PointToCell(const Vector3<float64>& point, const Vector3<float64>& origin,const Vector3<float64>& cellSize) noexcept;
	template Vector2<float32> CellMinimum(const Int2& cell, const Vector2<float32>& origin, const Vector2<float32>& cellSize) noexcept;
	template Vector2<float64> CellMinimum(const Int2& cell, const Vector2<float64>& origin, const Vector2<float64>& cellSize) noexcept;
	template Vector3<float32> CellMinimum(const Int3& cell, const Vector3<float32>& origin, const Vector3<float32>& cellSize) noexcept;
	template Vector3<float64> CellMinimum(const Int3& cell, const Vector3<float64>& origin, const Vector3<float64>& cellSize) noexcept;
} // namespace rox::math
