#include <GeometryVolumeQueries.h>
#include "VolumeDetail.hpp"
#include <Numeric.hpp>
#include <Comparison.hpp>

namespace rox::math
{
	template <MathScalar T>
	static bool volumeCylinderInterval(const detail::GeometryVector<T>& rawOffset, const detail::GeometryVector<T>& rawDirection, const detail::GeometryVector<T>& rawAxis, T radius, T maxParameter, T& enter, T& exit) noexcept
	{
		const T magnitude = core::Max({rawOffset.MaxAbs(), rawDirection.MaxAbs(), rawAxis.MaxAbs(), radius});
		int exponent = 0;
		std::frexp(magnitude, &exponent);
		const T scale = std::scalbn(T(1), exponent - 1);
		const detail::GeometryVector<T> offset = rawOffset / scale;
		const detail::GeometryVector<T> direction = rawDirection / scale;
		const detail::GeometryVector<T> axis = rawAxis / scale;
		radius /= scale;
		const T axisSquared = axis.LengthSquared();
		const T axialOrigin = offset.Dot(axis) / axisSquared;
		const T axialDirection = direction.Dot(axis) / axisSquared;
		enter = 0;
		exit = maxParameter;

		if (axialDirection == 0)
		{
			if (axialOrigin < 0 || axialOrigin > 1)
			{
				return false;
			}
		}
		else
		{
			const T first = -axialOrigin / axialDirection;
			const T second = (1 - axialOrigin) / axialDirection;
			enter = core::Max(enter, core::Min(first, second));
			exit = core::Min(exit, core::Max(first, second));

			if (enter > exit)
			{
				return false;
			}
		}

		const T inverseLength = radius == T(0) ? T(1) : T(1) / core::Sqrt(axisSquared);
		const detail::GeometryVector<T> radialOrigin = offset.Cross(axis) * inverseLength;
		const detail::GeometryVector<T> radialDirection = direction.Cross(axis) * inverseLength;
		const T speedSquared = radialDirection.LengthSquared();

		if (speedSquared == 0)
		{
			return radialOrigin.LengthSquared() <= radius * radius;
		}

		const T center = -radialOrigin.Dot(radialDirection) / speedSquared;
		const T closestDistance = core::Hypot(core::Fma(radialDirection.X, center, radialOrigin.X),
		                                           core::Fma(radialDirection.Y, center, radialOrigin.Y),
		                                           core::Fma(radialDirection.Z, center, radialOrigin.Z));

		if (closestDistance > radius)
		{
			return false;
		}

		const T halfWidth = core::Sqrt(((radius - closestDistance) * (radius + closestDistance)) / speedSquared);
		const T stableRoot = center + core::CopySign(halfWidth, center);
		const T originDistance = core::Hypot(radialOrigin.X, radialOrigin.Y, radialOrigin.Z);
		const T otherRoot = halfWidth == T(0) ? center
		                    : stableRoot == 0
		                        ? 0
		                        : (((originDistance - radius) * (originDistance + radius)) / speedSquared) / stableRoot;
		enter = core::Max(enter, core::Min(stableRoot, otherRoot));
		exit = core::Min(exit, core::Max(stableRoot, otherRoot));
		return enter <= exit;
	}

	template <MathScalar T>
	bool Intersect(const Ray<T>& ray, const Capsule<T>& capsule, RayInterval<T>* outInterval, T maxParameter) noexcept
	{
		ASSERT(outInterval != nullptr, "Intersect requires a valid output pointer.");
		ASSERT(ray.IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(capsule.IsValid(), "Capsule must be valid.");
		ASSERT(core::IsFinite(maxParameter) && maxParameter >= 0, "Ray query limit must be finite and non-negative.");

		if (capsule.Axis.Start == capsule.Axis.End)
		{
			return Intersect<T>(ray, Sphere<T>{capsule.Axis.Start, capsule.Radius}, outInterval, maxParameter);
		}

		const detail::GeometryVector<T> axis = detail::GeometryVector<T>(capsule.Axis.End) - detail::GeometryVector<T>(capsule.Axis.Start);
		const detail::GeometryVector<T> offset = detail::GeometryVector<T>(ray.Origin) - detail::GeometryVector<T>(capsule.Axis.Start);
		T enter = 0;
		T exit = maxParameter;
		bool hit = volumeCylinderInterval<T>(offset, detail::GeometryVector<T>(ray.Direction), axis, capsule.Radius, maxParameter, enter, exit);
		const Vector3<T> centers[]{capsule.Axis.Start, capsule.Axis.End};
		for (const Vector3<T>& center : centers)
		{
			RayInterval<T> sphereInterval;

			if (Intersect<T>(ray, Sphere<T>{center, capsule.Radius}, &(sphereInterval), maxParameter))
			{
				enter = hit ? core::Min(enter, static_cast<T>(sphereInterval.Enter)) : sphereInterval.Enter;
				exit = hit ? core::Max(exit, static_cast<T>(sphereInterval.Exit)) : sphereInterval.Exit;
				hit = true;
			}
		}

		if (!hit)
		{
			return false;
		}

		(*outInterval) = {static_cast<T>(enter), static_cast<T>(exit)};
		return true;
	}

	template <MathScalar T>
	bool Intersect(const Ray<T>& ray, const OrientedBox<T>& box, RayInterval<T>* outInterval, T maxParameter) noexcept
	{
		ASSERT(outInterval != nullptr, "Intersect requires a valid output pointer.");
		ASSERT(ray.IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(box.IsValid(), "Oriented box must be valid.");
		ASSERT(core::IsFinite(maxParameter) && maxParameter >= 0, "Ray query limit must be finite and non-negative.");

		const detail::VolumeFrame<T> frame = detail::VolumeRotation<T>(box.Rotation);
		const detail::GeometryVector<T> origin = detail::VolumeLocal<T>(frame, detail::GeometryVector<T>(ray.Origin) - detail::GeometryVector<T>(box.Center));
		const detail::GeometryVector<T> direction = detail::VolumeLocal<T>(frame, detail::GeometryVector<T>(ray.Direction));

		const T origins[]{origin.X, origin.Y, origin.Z};
		const T directions[]{direction.X, direction.Y, direction.Z};
		const T extents[]{box.HalfExtents.X, box.HalfExtents.Y, box.HalfExtents.Z};
		T enter = 0;
		T exit = maxParameter;

		for (uint32 axis = 0; axis < 3; ++axis)
		{
			if (directions[axis] == 0)
			{
				if (core::Abs(origins[axis]) > extents[axis])
				{
					return false;
				}

				continue;
			}

			const T first = (-extents[axis] - origins[axis]) / directions[axis];
			const T second = (extents[axis] - origins[axis]) / directions[axis];
			enter = core::Max(enter, core::Min(first, second));
			exit = core::Min(exit, core::Max(first, second));

			if (enter > exit)
			{
				return false;
			}
		}

		(*outInterval) = {static_cast<T>(enter), static_cast<T>(exit)};
		return true;
	}

	template bool Intersect(const Ray<float32>& ray, const Capsule<float32>& capsule, RayInterval<float32>* outInterval, float32 maxParameter) noexcept;
	template bool Intersect(const Ray<float64>& ray, const Capsule<float64>& capsule, RayInterval<float64>* outInterval, float64 maxParameter) noexcept;
	template bool Intersect(const Ray<float32>& ray, const OrientedBox<float32>& box, RayInterval<float32>* outInterval, float32 maxParameter) noexcept;
	template bool Intersect(const Ray<float64>& ray, const OrientedBox<float64>& box, RayInterval<float64>* outInterval, float64 maxParameter) noexcept;
} // namespace rox::math
