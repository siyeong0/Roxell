#include <GeometryVolumeQueries.h>
#include "VolumeDetail.hpp"
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

namespace rox::math
{
	template <MathScalar T, typename Volume>
	static EFrustumContainment volumeFrustumClassify(const Frustum<T>& frustum, const Volume& volume) noexcept
	{
		ASSERT(frustum.IsValid(0), "Geometry frustum must have finite planes with nonzero normals.");
		EFrustumContainment result = EFrustumContainment::Inside;

		for (uint32 index = 0; index < static_cast<uint32>(EFrustumPlane::Count); ++index)
		{
			const Plane<T>& plane = frustum.GetPlane(static_cast<EFrustumPlane>(index));

			if (plane.Normal == Vector3<T>::Zero())
			{
				continue;
			}

			const EPlaneSide side = Classify<T>(plane, volume);

			if (side == EPlaneSide::Negative)
			{
				return EFrustumContainment::Outside;
			}

			if (side == EPlaneSide::Intersecting)
			{
				result = EFrustumContainment::Intersecting;
			}
		}

		return result;
	}

	template <MathScalar T>
	EFrustumContainment Classify(const Frustum<T>& frustum, const Sphere<T>& sphere) noexcept
	{
		ASSERT(sphere.IsValid(), "Geometry sphere must be valid.");
		return volumeFrustumClassify<T>(frustum, sphere);
	}

	template <MathScalar T>
	EFrustumContainment Classify(const Frustum<T>& frustum, const OrientedBox<T>& box) noexcept
	{
		ASSERT(box.IsValid(), "Oriented box must be valid.");
		return volumeFrustumClassify<T>(frustum, box);
	}

	template <MathScalar T>
	EFrustumContainment Classify(const Frustum<T>& frustum, const Capsule<T>& capsule) noexcept
	{
		ASSERT(capsule.IsValid(), "Capsule must be valid.");
		return volumeFrustumClassify<T>(frustum, capsule);
	}

	template <MathScalar T>
	bool GetCorners(const Frustum<T>& frustum, FrustumCorners<T>* outCorners) noexcept
	{
		ASSERT(outCorners != nullptr, "GetCorners requires a valid output pointer.");
		ASSERT(frustum.IsValid(0), "Geometry frustum must have finite planes with nonzero normals.");
		FrustumCorners<T> result;
		Plane<T> planes[6];
		for (uint32 index = 0; index < 6; ++index)
		{
			if (!frustum.GetPlane(static_cast<EFrustumPlane>(index)).TryNormalize(&planes[index], T(0)))
			{
				return false;
			}
		}

		for (uint32 index = 0; index < 8; ++index)
		{
			const Plane<T>& xPlane = planes[(index & 1) ? 1 : 0];
			const Plane<T>& yPlane = planes[(index & 2) ? 3 : 2];
			const Plane<T>& zPlane = planes[(index & 4) ? 5 : 4];
			const detail::GeometryVector<T> x(xPlane.Normal);
			const detail::GeometryVector<T> y(yPlane.Normal);
			const detail::GeometryVector<T> z(zPlane.Normal);
			const T determinant = x.Dot(y.Cross(z));

			if (determinant == 0)
			{
				return false;
			}

			const detail::GeometryVector<T> point =
			    (y.Cross(z) * -static_cast<T>(xPlane.Distance) + z.Cross(x) * -static_cast<T>(yPlane.Distance) +
			     x.Cross(y) * -static_cast<T>(zPlane.Distance)) *
			    (1 / determinant);
			const T limit = rox::core::NumericLimits<T>::max();

			if (!rox::core::IsFinite(point.X) || !rox::core::IsFinite(point.Y) || !rox::core::IsFinite(point.Z) ||
			    rox::core::Abs(point.X) > limit || rox::core::Abs(point.Y) > limit || rox::core::Abs(point.Z) > limit)
			{
				return false;
			}

			result.Points[index] = {static_cast<T>(point.X), static_cast<T>(point.Y), static_cast<T>(point.Z)};
		}

		(*outCorners) = result;
		return true;
	}

	template <MathScalar T>
	static bool volumePlaneValid(const Plane<T>& plane) noexcept
	{
		return plane.Normal.IsFinite() && rox::core::IsFinite(plane.Distance) && (plane.Normal.X != 0 || plane.Normal.Y != 0 || plane.Normal.Z != 0);
	}

	template <MathScalar T>
	static T volumePlaneDistance(const Plane<T>& plane, const detail::GeometryVector<T>& point) noexcept
	{
		return detail::GeometryVector<T>(plane.Normal).Dot(point) + plane.Distance;
	}

	template <MathScalar T>
	static EPlaneSide volumePlaneSide(T lower, T upper) noexcept
	{
		if (lower > 0)
		{
			return EPlaneSide::Positive;
		}

		if (upper < 0)
		{
			return EPlaneSide::Negative;
		}

		return EPlaneSide::Intersecting;
	}

	template <MathScalar T>
	static T prepareRadiusPlane(Plane<T>& plane) noexcept
	{
		ASSERT(core::IsFinite(plane.Distance), "Geometry plane must have finite coefficients and a nonzero normal.");
		const T squared = detail::GeometryVector<T>(plane.Normal).LengthSquared();
		// A finite, positive squared length also establishes finite, nonzero normal components.
		if (core::IsFinite(squared) && squared >= core::NumericLimits<T>::min())
		{
			return squared;
		}

		ASSERT(volumePlaneValid<T>(plane), "Geometry plane must have finite coefficients and a nonzero normal.");
		const T scale = core::Max({core::Abs(plane.Normal.X), core::Abs(plane.Normal.Y), core::Abs(plane.Normal.Z)});
		plane.Normal = plane.Normal / scale;
		plane.Distance /= scale;
		return detail::GeometryVector<T>(plane.Normal).LengthSquared();
	}

	template <MathScalar T>
	EPlaneSide Classify(const Plane<T>& plane, const Sphere<T>& sphere) noexcept
	{
		ASSERT(sphere.IsValid(), "Geometry sphere must be valid.");
		Plane<T> working = plane;
		const T normalSquared = prepareRadiusPlane(working);
		const T distance = volumePlaneDistance<T>(working, detail::GeometryVector<T>(sphere.Center));
		const T support = core::Sqrt(normalSquared) * sphere.Radius;
		return volumePlaneSide<T>(distance - support, distance + support);
	}

	template <MathScalar T>
	EPlaneSide Classify(const Plane<T>& plane, const Box<T>& box) noexcept
	{
		ASSERT(volumePlaneValid<T>(plane), "Geometry plane must have finite coefficients and a nonzero normal.");
		ASSERT(box.IsValid(), "Geometry box must be valid.");
		// Evaluate axis support directly; endpoint sums and differences can overflow for finite boxes.
		const T x0 = plane.Normal.X * box.Min.X;
		const T x1 = plane.Normal.X * box.Max.X;
		const T y0 = plane.Normal.Y * box.Min.Y;
		const T y1 = plane.Normal.Y * box.Max.Y;
		const T z0 = plane.Normal.Z * box.Min.Z;
		const T z1 = plane.Normal.Z * box.Max.Z;
		const T lower = core::Min(x0, x1) + core::Min(y0, y1) + core::Min(z0, z1) + plane.Distance;
		const T upper = core::Max(x0, x1) + core::Max(y0, y1) + core::Max(z0, z1) + plane.Distance;
		return volumePlaneSide<T>(lower, upper);
	}

	template <MathScalar T>
	EPlaneSide Classify(const Plane<T>& plane, const OrientedBox<T>& box) noexcept
	{
		ASSERT(volumePlaneValid<T>(plane), "Geometry plane must have finite coefficients and a nonzero normal.");
		ASSERT(box.IsValid(), "Oriented box must be valid.");
		const detail::VolumeFrame<T> frame = detail::VolumeRotation<T>(box.Rotation);
		const detail::GeometryVector<T> normal(plane.Normal);
		const T distance = volumePlaneDistance<T>(plane, detail::GeometryVector<T>(box.Center));
		const T support = rox::core::Abs(normal.Dot(frame.Axes[0])) * box.HalfExtents.X +
		                  rox::core::Abs(normal.Dot(frame.Axes[1])) * box.HalfExtents.Y +
		                  rox::core::Abs(normal.Dot(frame.Axes[2])) * box.HalfExtents.Z;
		return volumePlaneSide<T>(distance - support, distance + support);
	}

	template <MathScalar T>
	EPlaneSide Classify(const Plane<T>& plane, const Capsule<T>& capsule) noexcept
	{
		ASSERT(capsule.IsValid(), "Capsule must be valid.");
		Plane<T> working = plane;
		const T normalSquared = prepareRadiusPlane(working);
		const T first = volumePlaneDistance<T>(working, detail::GeometryVector<T>(capsule.Axis.Start));
		const T second = volumePlaneDistance<T>(working, detail::GeometryVector<T>(capsule.Axis.End));
		const T support = core::Sqrt(normalSquared) * capsule.Radius;
		return volumePlaneSide<T>(core::Min(first, second) - support, core::Max(first, second) + support);
	}

	template EFrustumContainment Classify(const Frustum<float32>& frustum, const Sphere<float32>& sphere) noexcept;
	template EFrustumContainment Classify(const Frustum<float64>& frustum, const Sphere<float64>& sphere) noexcept;
	template EFrustumContainment Classify(const Frustum<float32>& frustum, const OrientedBox<float32>& box) noexcept;
	template EFrustumContainment Classify(const Frustum<float64>& frustum, const OrientedBox<float64>& box) noexcept;
	template EFrustumContainment Classify(const Frustum<float32>& frustum, const Capsule<float32>& capsule) noexcept;
	template EFrustumContainment Classify(const Frustum<float64>& frustum, const Capsule<float64>& capsule) noexcept;
	template bool GetCorners(const Frustum<float32>& frustum, FrustumCorners<float32>* outCorners) noexcept;
	template bool GetCorners(const Frustum<float64>& frustum, FrustumCorners<float64>* outCorners) noexcept;
	template EPlaneSide Classify(const Plane<float32>& plane, const Sphere<float32>& sphere) noexcept;
	template EPlaneSide Classify(const Plane<float64>& plane, const Sphere<float64>& sphere) noexcept;
	template EPlaneSide Classify(const Plane<float32>& plane, const Box<float32>& box) noexcept;
	template EPlaneSide Classify(const Plane<float64>& plane, const Box<float64>& box) noexcept;
	template EPlaneSide Classify(const Plane<float32>& plane, const OrientedBox<float32>& box) noexcept;
	template EPlaneSide Classify(const Plane<float64>& plane, const OrientedBox<float64>& box) noexcept;
	template EPlaneSide Classify(const Plane<float32>& plane, const Capsule<float32>& capsule) noexcept;
	template EPlaneSide Classify(const Plane<float64>& plane, const Capsule<float64>& capsule) noexcept;
} // namespace rox::math
