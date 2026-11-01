#pragma once

#include <MathScalar.hpp>

#include <LineSegment.h>
#include <TypeTraits.hpp>

#include <type_traits>

namespace rox::math
{
	template <MathScalar T = float32>
	struct Capsule final
	{
		LineSegment<T> Axis{};
		T Radius = T(0.0);

		constexpr Capsule() noexcept = default;

		constexpr Capsule(const LineSegment<T>& axis, T radius) noexcept : Axis(axis), Radius(radius)
		{
		}

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool Contains(const Vector3<T>& point) const noexcept;
		[[nodiscard]] Vector3<T> ClosestPoint(const Vector3<T>& point) const noexcept;
		[[nodiscard]] T DistanceSquared(const Vector3<T>& point) const noexcept;
		[[nodiscard]] Box<T> GetBounds() const noexcept;

		template <MathScalar U>
		explicit constexpr Capsule(const Capsule<U>& other) noexcept
		    : Axis(other.Axis), Radius(static_cast<T>(other.Radius))
		{
		}
	};

	static_assert(core::IsStandardLayout<Capsule<float32>> && core::IsTriviallyCopyable<Capsule<float32>>);
	static_assert(core::IsStandardLayout<Capsule<float64>> && core::IsTriviallyCopyable<Capsule<float64>>);
} // namespace rox::math
