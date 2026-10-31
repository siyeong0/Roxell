#pragma once

#include <MathScalar.hpp>
#include <Vector2.hpp>
#include <Vector3.hpp>
#include <Vector4.hpp>

namespace rox::math
{
	template <typename T>
	struct SpringState final
	{
		T Value{};
		T Velocity{};
	};

	// Frequency is cycles/second; velocity is units/second. The target is constant over this nonnegative time step.
	// Inputs are finite; frequency and damping ratio are nonnegative. Zero frequency is ballistic; zero time preserves
	// state.
	template <MathScalar T = float32>
	[[nodiscard]] SpringState<T> SpringStep(SpringState<T> current, T target, T frequencyHz, T dampingRatio, T deltaSeconds) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] SpringState<Vector2<T>> SpringStep(SpringState<Vector2<T>> current, const Vector2<T>& target, T frequencyHz, T dampingRatio, T deltaSeconds) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] SpringState<Vector3<T>> SpringStep(SpringState<Vector3<T>> current, const Vector3<T>& target, T frequencyHz, T dampingRatio, T deltaSeconds) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] SpringState<Vector4<T>> SpringStep(SpringState<Vector4<T>> current, const Vector4<T>& target, T frequencyHz, T dampingRatio, T deltaSeconds) noexcept;
} // namespace rox::math
