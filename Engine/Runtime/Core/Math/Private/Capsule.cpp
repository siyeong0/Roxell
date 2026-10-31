#include <Capsule.h>
#include "VolumeDetail.hpp"
#include <Numeric.hpp>
#include <Comparison.hpp>

namespace rox::math
{
	template <MathScalar T>
	bool Capsule<T>::IsValid() const noexcept
	{
		return Axis.IsValid() && rox::core::IsFinite(Radius) && Radius >= T(0.0);
	}

	template <MathScalar T>
	bool Capsule<T>::Contains(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Capsule must be valid.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		return detail::VolumePointSegmentQuery<T, true>(Axis, point, Radius);
	}

	template <MathScalar T>
	Vector3<T> Capsule<T>::ClosestPoint(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Capsule must be valid.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const detail::VolumeSegmentPairResult<T> pair = detail::VolumePointSegmentQuery<T, false>(Axis, point);
		const detail::GeometryVector<T> offset = pair.Second - pair.First;
		const T distance = rox::core::Hypot(offset.X, offset.Y, offset.Z);

		if (distance <= Radius)
		{
			return point;
		}

		return detail::GeometryPoint<T>(pair.First + offset * (Radius / distance));
	}

	template <MathScalar T>
	T Capsule<T>::DistanceSquared(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Capsule must be valid.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const detail::VolumeSegmentPairResult<T> pair = detail::VolumePointSegmentQuery<T, false>(Axis, point);
		const detail::GeometryVector<T> offset = pair.Second - pair.First;
		const T gap = rox::core::Max(T(0.0), rox::core::Hypot(offset.X, offset.Y, offset.Z) - Radius);
		return detail::GeometryMagnitude<T>(gap * gap);
	}

	template <MathScalar T>
	Box<T> Capsule<T>::GetBounds() const noexcept
	{
		ASSERT(IsValid(), "Capsule must be valid.");
		const T radius = static_cast<T>(Radius);
		return {{detail::VolumeOutward<T>(rox::core::Min(Axis.Start.X, Axis.End.X), -radius, false),
		         detail::VolumeOutward<T>(rox::core::Min(Axis.Start.Y, Axis.End.Y), -radius, false),
		         detail::VolumeOutward<T>(rox::core::Min(Axis.Start.Z, Axis.End.Z), -radius, false)},
		        {detail::VolumeOutward<T>(rox::core::Max(Axis.Start.X, Axis.End.X), radius, true),
		         detail::VolumeOutward<T>(rox::core::Max(Axis.Start.Y, Axis.End.Y), radius, true),
		         detail::VolumeOutward<T>(rox::core::Max(Axis.Start.Z, Axis.End.Z), radius, true)}};
	}

	template struct Capsule<float32>;
	template struct Capsule<float64>;
} // namespace rox::math
