#include <GeometryVolumeQueries.h>
#include "VolumeDetail.hpp"
#include <Numeric.hpp>
#include <Comparison.hpp>

namespace rox::math
{
	template <MathScalar T>
	static bool volumeBoxAdmitted(const Box<T>& box) noexcept
	{
		return box.IsValid() || (box.Min == Box<T>::Empty().Min && box.Max == Box<T>::Empty().Max);
	}

	template <MathScalar T>
	Box<T> Union(const Box<T>& first, const Box<T>& second) noexcept
	{
		ASSERT(volumeBoxAdmitted<T>(first) && volumeBoxAdmitted<T>(second), "Geometry box must be valid or empty.");

		if (!first.IsValid())
		{
			return second;
		}

		if (!second.IsValid())
		{
			return first;
		}

		return {{rox::core::Min(first.Min.X, second.Min.X),  rox::core::Min(first.Min.Y, second.Min.Y), rox::core::Min(first.Min.Z, second.Min.Z)},
		        {rox::core::Max(first.Max.X, second.Max.X),  rox::core::Max(first.Max.Y, second.Max.Y), rox::core::Max(first.Max.Z, second.Max.Z)}};
	}

	template <MathScalar T>
	bool Intersection(const Box<T>& first, const Box<T>& second, Box<T>* outIntersection) noexcept
	{
		ASSERT(outIntersection != nullptr, "Intersection requires a valid output pointer.");
		ASSERT(volumeBoxAdmitted<T>(first) && volumeBoxAdmitted<T>(second), "Geometry box must be valid or empty.");

		if (!first.IsValid() || !second.IsValid())
		{
			return false;
		}

		const Box<T> result{{rox::core::Max(first.Min.X, second.Min.X), rox::core::Max(first.Min.Y, second.Min.Y), rox::core::Max(first.Min.Z, second.Min.Z)},
		                    {rox::core::Min(first.Max.X, second.Max.X), rox::core::Min(first.Max.Y, second.Max.Y), rox::core::Min(first.Max.Z, second.Max.Z)}};
		if (!result.IsValid())
		{
			return false;
		}

		(*outIntersection) = result;
		return true;
	}

	template <MathScalar T>
	Box<T> Expanded(const Box<T>& box, const Vector3<T>& amount) noexcept
	{
		ASSERT(volumeBoxAdmitted<T>(box), "Geometry box must be valid or empty.");
		ASSERT(amount.IsFinite() && amount.X >= 0 && amount.Y >= 0 && amount.Z >= 0, "Geometry expansion must be finite and non-negative.");

		if (!box.IsValid())
		{
			return box;
		}

		return {{detail::VolumeOutward<T>(static_cast<T>(box.Min.X), -amount.X, false), 
				 detail::VolumeOutward<T>(static_cast<T>(box.Min.Y), -amount.Y, false), 
				 detail::VolumeOutward<T>(static_cast<T>(box.Min.Z), -amount.Z, false)},
		        {detail::VolumeOutward<T>(static_cast<T>(box.Max.X), amount.X, true), 
				 detail::VolumeOutward<T>(static_cast<T>(box.Max.Y), amount.Y, true), 
				 detail::VolumeOutward<T>(static_cast<T>(box.Max.Z), amount.Z, true)}};
	}

	template <MathScalar T>
	SegmentClosestPoints<T> ClosestPoints(const LineSegment<T>& first, const LineSegment<T>& second) noexcept
	{
		ASSERT(first.IsValid() && second.IsValid(), "Segment endpoints must be finite.");
		const detail::VolumeSegmentPairResult<T> closest = detail::VolumeSegmentPair<T>(first, second);
		return {detail::GeometryPoint<T>(closest.First), detail::GeometryPoint<T>(closest.Second), static_cast<T>(closest.FirstParameter), static_cast<T>(closest.SecondParameter)};
	}

	template <MathScalar T>
	T DistanceSquared(const LineSegment<T>& first, const LineSegment<T>& second) noexcept
	{
		ASSERT(first.IsValid() && second.IsValid(), "Segment endpoints must be finite.");
		const detail::VolumeSegmentPairResult<T> closest = detail::VolumeSegmentPair<T>(first, second);
		return detail::GeometryMagnitude<T>((closest.First - closest.Second).LengthSquared());
	}

	template <MathScalar T>
	bool Overlaps(const Sphere<T>& sphere, const Box<T>& box) noexcept
	{
		ASSERT(sphere.Center.IsFinite() && core::IsFinite(sphere.Radius) && sphere.Radius >= T(0), "Geometry sphere must be valid.");
		ASSERT(box.IsValid(), "Geometry box must be valid.");
		const detail::GeometryVector<T> nearest{rox::core::ClampValue(sphere.Center.X, box.Min.X, box.Max.X), 
											    rox::core::ClampValue(sphere.Center.Y, box.Min.Y, box.Max.Y), 
											    rox::core::ClampValue(sphere.Center.Z, box.Min.Z, box.Max.Z)};
		return detail::GeometryWithinRadii(detail::GeometryVector<T>(sphere.Center), nearest, sphere.Radius);
	}

	template <MathScalar T>
	bool Overlaps(const OrientedBox<T>& box, const Sphere<T>& sphere) noexcept
	{
		ASSERT(box.IsValid(), "Oriented box must be valid.");
		ASSERT(sphere.Center.IsFinite() && core::IsFinite(sphere.Radius) && sphere.Radius >= T(0), "Geometry sphere must be valid.");
		const detail::GeometryVector<T> local = detail::VolumeLocal<T>(detail::VolumeRotation<T>(box.Rotation), detail::GeometryVector<T>(sphere.Center) - detail::GeometryVector<T>(box.Center));
		const detail::GeometryVector<T> gap{rox::core::Max(T(0.0), rox::core::Abs(local.X) - box.HalfExtents.X), 
											rox::core::Max(T(0.0), rox::core::Abs(local.Y) - box.HalfExtents.Y), 
											rox::core::Max(T(0.0), rox::core::Abs(local.Z) - box.HalfExtents.Z)};
		return detail::GeometryWithinRadii(gap, detail::GeometryVector<T>{0, 0, 0}, sphere.Radius);
	}

	template <MathScalar T>
	bool Overlaps(const Capsule<T>& capsule, const Sphere<T>& sphere) noexcept
	{
		ASSERT(capsule.Axis.IsValid() && core::IsFinite(capsule.Radius) && capsule.Radius >= T(0), "Capsule must be valid.");
		ASSERT(sphere.Center.IsFinite() && core::IsFinite(sphere.Radius) && sphere.Radius >= T(0), "Geometry sphere must be valid.");
		return detail::VolumePointSegmentQuery<T, true>(capsule.Axis, sphere.Center, capsule.Radius, sphere.Radius);
	}

	template <MathScalar T>
	bool Overlaps(const OrientedBox<T>& first, const OrientedBox<T>& second) noexcept
	{
		ASSERT(first.IsValid() && second.IsValid(), "Oriented box must be valid.");
		const detail::VolumeFrame<T> firstFrame = detail::VolumeRotation<T>(first.Rotation);
		const detail::VolumeFrame<T> secondFrame = detail::VolumeRotation<T>(second.Rotation);
		const detail::GeometryVector<T> offset = detail::GeometryVector<T>(second.Center) - detail::GeometryVector<T>(first.Center);
		const auto separated = [&](const detail::GeometryVector<T>& axis)
		{
			const T firstRadius = rox::core::Abs(firstFrame.Axes[0].Dot(axis)) * first.HalfExtents.X +
			                      rox::core::Abs(firstFrame.Axes[1].Dot(axis)) * first.HalfExtents.Y +
			                      rox::core::Abs(firstFrame.Axes[2].Dot(axis)) * first.HalfExtents.Z;
			const T secondRadius = rox::core::Abs(secondFrame.Axes[0].Dot(axis)) * second.HalfExtents.X +
			                       rox::core::Abs(secondFrame.Axes[1].Dot(axis)) * second.HalfExtents.Y +
			                       rox::core::Abs(secondFrame.Axes[2].Dot(axis)) * second.HalfExtents.Z;
			return rox::core::Abs(offset.Dot(axis)) > firstRadius + secondRadius;
		};
		for (uint32 axis = 0; axis < 3; ++axis)
		{
			if (separated(firstFrame.Axes[axis]) || separated(secondFrame.Axes[axis]))
			{
				return false;
			}
		}

		for (uint32 firstAxis = 0; firstAxis < 3; ++firstAxis)
		{
			for (uint32 secondAxis = 0; secondAxis < 3; ++secondAxis)
			{
				if (separated(firstFrame.Axes[firstAxis].Cross(secondFrame.Axes[secondAxis])))
				{
					return false;
				}
			}
		}

		return true;
	}

	template <MathScalar T>
	bool Overlaps(const Capsule<T>& first, const Capsule<T>& second) noexcept
	{
		ASSERT(first.Axis.IsValid() && second.Axis.IsValid() && core::IsFinite(first.Radius) && core::IsFinite(second.Radius) && first.Radius >= T(0) && second.Radius >= T(0), "Capsule must be valid.");
		return detail::VolumeSegmentsWithinRadii(first.Axis, second.Axis, first.Radius, second.Radius);
	}

	template <MathScalar T>
	T DistanceSquared(const Capsule<T>& first, const Capsule<T>& second) noexcept
	{
		ASSERT(first.IsValid() && second.IsValid(), "Capsule must be valid.");
		const detail::VolumeSegmentPairResult<T> pair = detail::VolumeSegmentPair<T>(first.Axis, second.Axis);
		const detail::GeometryVector<T> offset = pair.First - pair.Second;
		const T radius = static_cast<T>(first.Radius) + second.Radius;
		const T gap = rox::core::Max(T(0.0), rox::core::Hypot(offset.X, offset.Y, offset.Z) - radius);
		return detail::GeometryMagnitude<T>(gap * gap);
	}

	template Box<float32> Union(const Box<float32>& first, const Box<float32>& second) noexcept;
	template Box<float64> Union(const Box<float64>& first, const Box<float64>& second) noexcept;
	template bool Intersection(const Box<float32>& first, const Box<float32>& second, Box<float32>* outIntersection) noexcept;
	template bool Intersection(const Box<float64>& first, const Box<float64>& second, Box<float64>* outIntersection) noexcept;
	template Box<float32> Expanded(const Box<float32>& box, const Vector3<float32>& amount) noexcept;
	template Box<float64> Expanded(const Box<float64>& box, const Vector3<float64>& amount) noexcept;
	template SegmentClosestPoints<float32> ClosestPoints(const LineSegment<float32>& first, const LineSegment<float32>& second) noexcept;
	template SegmentClosestPoints<float64> ClosestPoints(const LineSegment<float64>& first, const LineSegment<float64>& second) noexcept;
	template float32 DistanceSquared(const LineSegment<float32>& first, const LineSegment<float32>& second) noexcept;
	template float64 DistanceSquared(const LineSegment<float64>& first, const LineSegment<float64>& second) noexcept;
	template bool Overlaps(const Sphere<float32>& sphere, const Box<float32>& box) noexcept;
	template bool Overlaps(const Sphere<float64>& sphere, const Box<float64>& box) noexcept;
	template bool Overlaps(const OrientedBox<float32>& box, const Sphere<float32>& sphere) noexcept;
	template bool Overlaps(const OrientedBox<float64>& box, const Sphere<float64>& sphere) noexcept;
	template bool Overlaps(const Capsule<float32>& capsule, const Sphere<float32>& sphere) noexcept;
	template bool Overlaps(const Capsule<float64>& capsule, const Sphere<float64>& sphere) noexcept;
	template bool Overlaps(const OrientedBox<float32>& first, const OrientedBox<float32>& second) noexcept;
	template bool Overlaps(const OrientedBox<float64>& first, const OrientedBox<float64>& second) noexcept;
	template bool Overlaps(const Capsule<float32>& first, const Capsule<float32>& second) noexcept;
	template bool Overlaps(const Capsule<float64>& first, const Capsule<float64>& second) noexcept;
	template float32 DistanceSquared(const Capsule<float32>& first, const Capsule<float32>& second) noexcept;
	template float64 DistanceSquared(const Capsule<float64>& first, const Capsule<float64>& second) noexcept;
} // namespace rox::math
