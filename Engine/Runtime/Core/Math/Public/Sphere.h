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
	struct Sphere final
	{
		Vector3<T> Center = Vector3<T>::Zero();
		T Radius = T(0.0);

		constexpr Sphere() noexcept = default;
		Sphere(const Vector3<T>& center, T radius) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool Contains(const Vector3<T>& point) const noexcept;
		[[nodiscard]] bool Overlaps(const Sphere<T>& other) const noexcept;
		[[nodiscard]] bool TryEncapsulate(const Vector3<T>& point) noexcept;
		void Encapsulate(const Vector3<T>& point) noexcept;

		template <MathScalar U>
		explicit constexpr Sphere(const Sphere<U>& other) noexcept
		    : Center(other.Center)
			, Radius(static_cast<T>(other.Radius))
		{
		}
	};

	static_assert(sizeof(Sphere<float32>) == sizeof(float32) * 4);
	static_assert(sizeof(Sphere<float64>) == sizeof(float64) * 4);
	static_assert(offsetof(Sphere<float32>, Radius) == sizeof(float32) * 3);
	static_assert(offsetof(Sphere<float64>, Radius) == sizeof(float64) * 3);
	static_assert(rox::core::IsStandardLayout<Sphere<float32>> && rox::core::IsTriviallyCopyable<Sphere<float32>>);
	static_assert(rox::core::IsStandardLayout<Sphere<float64>> && rox::core::IsTriviallyCopyable<Sphere<float64>>);
} // namespace rox::math
