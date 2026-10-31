#pragma once

#include <MathScalar.hpp>
#include <MathConstants.hpp>

#include <Types.h>

namespace rox::math
{
	template <MathScalar T = float32>
	[[nodiscard]] constexpr T DegreesToRadians(T degrees) noexcept
	{
		return degrees * (PI<T> / T(180.0));
	}

	template <MathScalar T = float32>
	[[nodiscard]] constexpr T RadiansToDegrees(T radians) noexcept
	{
		return radians * (T(180.0) / PI<T>);
	}

	template <MathScalar T = float32>
	[[nodiscard]] T Clamp(T value, T minimum, T maximum) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T Saturate(T value) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T Lerp(T from, T to, T alpha) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T InverseLerp(T from, T to, T value) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T Remap(T value, T inputMinimum, T inputMaximum, T outputMinimum, T outputMaximum) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool NearlyEqual(T left, T right, T absoluteTolerance = T(1.0e-6),
	                               T relativeTolerance = T(1.0e-6)) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool IsNearlyZero(T value, T tolerance = T(1.0e-6)) noexcept;

	// Wrap excludes maximum; Repeat excludes length. Negative inputs wrap toward the positive interval.
	template <MathScalar T = float32>
	[[nodiscard]] T Wrap(T value, T minimum, T maximum) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T Repeat(T value, T length) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T PingPong(T value, T length) noexcept;

	// Angles are radians. The shortest signed delta is in [-pi, pi).
	template <MathScalar T = float32>
	[[nodiscard]] T DeltaAngle(T from, T to) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T LerpAngle(T from, T to, T alpha) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T MoveTowards(T current, T target, T maximumDelta) noexcept;

	// Smooth interpolation clamps the normalized parameter; descending distinct edges are supported.
	template <MathScalar T = float32>
	[[nodiscard]] T SmoothStep(T edge0, T edge1, T value) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T SmootherStep(T edge0, T edge1, T value) noexcept;

	// Step is positive. Halfway values round away from zero.
	template <MathScalar T = float32>
	[[nodiscard]] T Snap(T value, T step) noexcept;
} // namespace rox::math
