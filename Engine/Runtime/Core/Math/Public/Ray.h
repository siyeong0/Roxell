#pragma once

#include <MathScalar.hpp>

#include <TypeTraits.hpp>
#include <Vector3.hpp>

#include <type_traits>

namespace rox::math
{
	template <MathScalar T = float32>
	struct Ray final
	{
		Vector3<T> Origin = Vector3<T>::Zero();
		Vector3<T> Direction = Vector3<T>::Forward();

		constexpr Ray() noexcept = default;

		constexpr Ray(const Vector3<T>& origin, const Vector3<T>& direction) noexcept
		    : Origin(origin)
			, Direction(direction)
		{
		}

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] Vector3<T> At(T parameter) const noexcept;

		// Saturates at the largest finite T; point and distance queries avoid materializing an overflowing parameter.
		[[nodiscard]] T ClosestParameter(const Vector3<T>& point) const noexcept;
		[[nodiscard]] Vector3<T> ClosestPoint(const Vector3<T>& point) const noexcept;
		[[nodiscard]] T DistanceSquared(const Vector3<T>& point) const noexcept;

		template <MathScalar U>
		explicit constexpr Ray(const Ray<U>& other) noexcept : Origin(other.Origin), Direction(other.Direction)
		{
		}
	};

	static_assert(sizeof(Ray<float32>) == 2 * sizeof(Vector3<float32>));
	static_assert(sizeof(Ray<float64>) == 2 * sizeof(Vector3<float64>));
	static_assert(core::IsStandardLayout<Ray<float32>> && core::IsTriviallyCopyable<Ray<float32>>);
	static_assert(core::IsStandardLayout<Ray<float64>> && core::IsTriviallyCopyable<Ray<float64>>);
} // namespace rox::math
