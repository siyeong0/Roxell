#pragma once

#include <MathScalar.hpp>
#include <Vector2.hpp>
#include <Vector3.hpp>
#include <Vector4.hpp>

namespace rox::math
{
	// Values are finite and times are finite/nonnegative. Zero elapsed time preserves current; zero half-life otherwise
	// selects target.
	template <MathScalar T = float32>
	[[nodiscard]] T ExponentialDamp(T current, T target, T halfLifeSeconds, T deltaSeconds) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> ExponentialDamp(const Vector2<T>& current, const Vector2<T>& target, T halfLifeSeconds, T deltaSeconds) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> ExponentialDamp(const Vector3<T>& current, const Vector3<T>& target, T halfLifeSeconds, T deltaSeconds) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector4<T> ExponentialDamp(const Vector4<T>& current, const Vector4<T>& target, T halfLifeSeconds, T deltaSeconds) noexcept;
} // namespace rox::math
