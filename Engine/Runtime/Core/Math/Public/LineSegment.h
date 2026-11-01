#pragma once

#include <MathScalar.hpp>

#include <Box.h>
#include <TypeTraits.hpp>

namespace rox::math
{
	template <MathScalar T = float32>
	struct LineSegment final
	{
		Vector3<T> Start = Vector3<T>::Zero();
		Vector3<T> End = Vector3<T>::Zero();
		constexpr LineSegment() noexcept = default;

		constexpr LineSegment(const Vector3<T>& start, const Vector3<T>& end) noexcept : Start(start), End(end)
		{
		}

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] Vector3<T> At(T parameter) const noexcept;
		[[nodiscard]] T Length() const noexcept;
		[[nodiscard]] T LengthSquared() const noexcept;
		[[nodiscard]] T ClosestParameter(const Vector3<T>& point) const noexcept;
		[[nodiscard]] Vector3<T> ClosestPoint(const Vector3<T>& point) const noexcept;
		[[nodiscard]] T DistanceSquared(const Vector3<T>& point) const noexcept;
		[[nodiscard]] Box<T> GetBounds() const noexcept;

		template <MathScalar U>
		explicit constexpr LineSegment(const LineSegment<U>& other) noexcept : Start(other.Start), End(other.End)
		{
		}
	};

	static_assert(sizeof(LineSegment<float32>) == 2 * sizeof(Vector3<float32>));
	static_assert(sizeof(LineSegment<float64>) == 2 * sizeof(Vector3<float64>));
	static_assert(core::IsStandardLayout<LineSegment<float32>> && core::IsTriviallyCopyable<LineSegment<float32>>);
	static_assert(core::IsStandardLayout<LineSegment<float64>> && core::IsTriviallyCopyable<LineSegment<float64>>);
} // namespace rox::math
