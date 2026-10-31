#pragma once

#include <MathScalar.hpp>

#include <Int2.hpp>
#include <Int3.hpp>
#include <Types.h>
#include <Vector2.hpp>
#include <Vector3.hpp>

namespace rox::math
{
	// Divisors must be positive. Signed division rounds down and its remainder is nonnegative.
	[[nodiscard]] int32 FloorDiv(int32 value, int32 divisor) noexcept;
	[[nodiscard]] int32 FloorMod(int32 value, int32 divisor) noexcept;
	[[nodiscard]] uint32 DivideRoundUp(uint32 value, uint32 divisor) noexcept;

	// Inputs must be finite, cell sizes positive, and final cell/point components representable.
	template <MathScalar T = float32>
	[[nodiscard]] Int2 PointToCell(const Vector2<T>& point, const Vector2<T>& origin, const Vector2<T>& cellSize) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Int3 PointToCell(const Vector3<T>& point, const Vector3<T>& origin, const Vector3<T>& cellSize) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> CellMinimum(const Int2& cell, const Vector2<T>& origin, const Vector2<T>& cellSize) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> CellMinimum(const Int3& cell, const Vector3<T>& origin, const Vector3<T>& cellSize) noexcept;
} // namespace rox::math
