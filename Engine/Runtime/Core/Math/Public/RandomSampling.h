#pragma once

#include <MathScalar.hpp>

#include <Types.h>
#include <RandomStream.h>
#include <Vector2.hpp>
#include <Vector3.hpp>

#include <cstddef>

namespace rox::math
{
	template <MathScalar T = float32>
	[[nodiscard]] rox::core::usize RandomWeightedIndex(RandomStream& stream, const T* weights, rox::core::usize count) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> RandomUnitCircle(RandomStream& stream) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> RandomInUnitDisk(RandomStream& stream) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> RandomUnitSphere(RandomStream& stream) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> RandomInUnitSphere(RandomStream& stream) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> RandomCone(RandomStream& stream, const Vector3<T>& unitAxis, T halfAngleRadians) noexcept;

	// Standard normal and unit-rate exponential; no cached samples or additional stream state.
	template <MathScalar T = float32>
	[[nodiscard]] T RandomNormal(RandomStream& stream) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T RandomExponential(RandomStream& stream) noexcept;
} // namespace rox::math
