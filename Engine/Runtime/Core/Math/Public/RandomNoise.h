#pragma once

#include <MathScalar.hpp>

#include <Types.h>
#include <Vector2.hpp>
#include <Vector3.hpp>

namespace rox::math
{
	// Stateless samples depend only on the seed, stream and counter, not execution order.
	[[nodiscard]] uint64 RandomCounterU64(uint64 seed, uint64 counter, uint64 streamSelector = 0) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T RandomCounterFloat01(uint64 seed, uint64 counter, uint64 streamSelector = 0) noexcept;

	// Quintic interpolated value noise in [0,1]; finite input coordinates must be within +/-1048576.
	template <MathScalar T = float32>
	[[nodiscard]] T ValueNoise2D(const Vector2<T>& position, uint64 seed = 0) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ValueNoise3D(const Vector3<T>& position, uint64 seed = 0) noexcept;
} // namespace rox::math
