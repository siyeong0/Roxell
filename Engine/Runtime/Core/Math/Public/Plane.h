#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Vector3.hpp>

#include <cstddef>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T = float32>
	struct Plane final
	{
		Vector3<T> Normal = Vector3<T>::Zero();
		T Distance = T(0.0);

		constexpr Plane() noexcept = default;

		constexpr Plane(const Vector3<T>& normal, T distance) noexcept : Normal(normal), Distance(distance)
		{
		}

		[[nodiscard]] static Plane<T> FromPointNormal(const Vector3<T>& point, const Vector3<T>& normal) noexcept;

		[[nodiscard]] constexpr T SignedDistance(const Vector3<T>& point) const noexcept
		{
			return Normal.Dot(point) + Distance;
		}

		[[nodiscard]] bool TryNormalize(Plane<T>* outNormalized, T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] Plane<T> Normalized(T epsilon = T(1.0e-8)) const noexcept;

		void Normalize(T epsilon = T(1.0e-8)) noexcept
		{
			*this = Normalized(epsilon);
		}

		template <MathScalar U>
		explicit constexpr Plane(const Plane<U>& other) noexcept
		    : Normal(other.Normal), Distance(static_cast<T>(other.Distance))
		{
		}
	};

	static_assert(sizeof(Plane<float32>) == sizeof(float32) * 4);
	static_assert(sizeof(Plane<float64>) == sizeof(float64) * 4);
	static_assert(offsetof(Plane<float32>, Distance) == sizeof(float32) * 3);
	static_assert(offsetof(Plane<float64>, Distance) == sizeof(float64) * 3);
	static_assert(rox::core::IsStandardLayout<Plane<float32>> && rox::core::IsTriviallyCopyable<Plane<float32>>);
	static_assert(rox::core::IsStandardLayout<Plane<float64>> && rox::core::IsTriviallyCopyable<Plane<float64>>);
} // namespace rox::math
