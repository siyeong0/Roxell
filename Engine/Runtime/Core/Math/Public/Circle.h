#pragma once

#include <MathScalar.hpp>

#include <TypeTraits.hpp>
#include <Vector2.hpp>

#include <type_traits>

namespace rox::math
{
	template <MathScalar T = float32>
	struct Circle final
	{
		Vector2<T> Center = Vector2<T>::Zero();
		T Radius = T(0.0);

		constexpr Circle() noexcept = default;

		constexpr Circle(const Vector2<T>& center, T radius) noexcept : Center(center), Radius(radius)
		{
		}

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool Contains(const Vector2<T>& point) const noexcept;
		[[nodiscard]] bool Overlaps(const Circle<T>& other) const noexcept;
		[[nodiscard]] Vector2<T> ClosestPoint(const Vector2<T>& point) const noexcept;
		[[nodiscard]] T DistanceSquared(const Vector2<T>& point) const noexcept;

		template <MathScalar U>
		explicit constexpr Circle(const Circle<U>& other) noexcept
		    : Center(other.Center), Radius(static_cast<T>(other.Radius))
		{
		}
	};

	static_assert(core::IsStandardLayout<Circle<float32>> && core::IsTriviallyCopyable<Circle<float32>>);
	static_assert(core::IsStandardLayout<Circle<float64>> && core::IsTriviallyCopyable<Circle<float64>>);
} // namespace rox::math
