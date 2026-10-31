#include <Circle.h>
#include "GeometryDetail.hpp"
#include <Numeric.hpp>

namespace rox::math
{
	template <MathScalar T>
	bool Circle<T>::IsValid() const noexcept
	{
		return Center.IsFinite() && rox::core::IsFinite(Radius) && Radius >= T(0.0);
	}

	template <MathScalar T>
	bool Circle<T>::Contains(const Vector2<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Circle must be valid.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const T x = static_cast<T>(point.X) - Center.X;
		const T y = static_cast<T>(point.Y) - Center.Y;
		return rox::core::Hypot(x, y) <= Radius;
	}

	template <MathScalar T>
	bool Circle<T>::Overlaps(const Circle<T>& other) const noexcept
	{
		ASSERT(IsValid() && other.IsValid(), "Circle must be valid.");
		const T x = static_cast<T>(other.Center.X) - Center.X;
		const T y = static_cast<T>(other.Center.Y) - Center.Y;
		const T radius = static_cast<T>(Radius) + other.Radius;
		if (rox::core::IsFinite(radius))
		{
			return rox::core::Hypot(x, y) <= radius;
		}

		return rox::core::Hypot(other.Center.X * T(0.5) - Center.X * T(0.5), other.Center.Y * T(0.5) - Center.Y * T(0.5)) <= Radius * T(0.5) + other.Radius * T(0.5);
	}

	template <MathScalar T>
	Vector2<T> Circle<T>::ClosestPoint(const Vector2<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Circle must be valid.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const T x = static_cast<T>(point.X) - Center.X;
		const T y = static_cast<T>(point.Y) - Center.Y;
		const T distance = rox::core::Hypot(x, y);

		if (distance <= Radius)
		{
			return point;
		}

		const T scale = Radius / distance;
		return {detail::GeometryCoordinate<T>(Center.X + x * scale), detail::GeometryCoordinate<T>(Center.Y + y * scale)};
	}

	template <MathScalar T>
	T Circle<T>::DistanceSquared(const Vector2<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Circle must be valid.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const T x = static_cast<T>(point.X) - Center.X;
		const T y = static_cast<T>(point.Y) - Center.Y;
		const T distance = rox::core::Hypot(x, y);
		const T gap = distance > Radius ? distance - Radius : T(0.0);
		return detail::GeometryMagnitude<T>(gap * gap);
	}

	template struct Circle<float32>;
	template struct Circle<float64>;
} // namespace rox::math
