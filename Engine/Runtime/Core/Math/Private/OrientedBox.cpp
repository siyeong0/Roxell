#include <OrientedBox.h>
#include "VolumeDetail.hpp"
#include <GeometryQueries.h>
#include <Numeric.hpp>
#include <Comparison.hpp>

#include <algorithm>

namespace rox::math
{
	template <MathScalar T>
	bool OrientedBox<T>::IsValid() const noexcept
	{
		return Center.IsFinite() && HalfExtents.IsFinite() && HalfExtents.X >= 0 && HalfExtents.Y >= 0 && HalfExtents.Z >= 0 && Rotation.IsFinite() && (Rotation.X != 0 || Rotation.Y != 0 || Rotation.Z != 0 || Rotation.W != 0);
	}

	template <MathScalar T>
	Vector3<T> OrientedBox<T>::GetCorner(uint32 index) const noexcept
	{
		ASSERT(IsValid(), "Oriented box must be valid.");
		ASSERT(index < 8, "Geometry corner index must be below eight.");
		const detail::GeometryVector<T> local{(index & 1) ? HalfExtents.X : -HalfExtents.X,
		                              		  (index & 2) ? HalfExtents.Y : -HalfExtents.Y,
		                              		  (index & 4) ? HalfExtents.Z : -HalfExtents.Z};
		return detail::GeometryPoint<T>(detail::GeometryVector<T>(Center) + detail::VolumeWorld<T>(detail::VolumeRotation<T>(Rotation), local));
	}

	template <MathScalar T>
	Box<T> OrientedBox<T>::GetBounds() const noexcept
	{
		ASSERT(IsValid(), "Oriented box must be valid.");
		const detail::VolumeFrame<T> frame = detail::VolumeRotation<T>(Rotation);
		const Matrix3x3<T> linear = Matrix3x3<T>::FromRows(detail::GeometryPoint<T>(frame.Axes[0]), detail::GeometryPoint<T>(frame.Axes[1]), detail::GeometryPoint<T>(frame.Axes[2]));
		const core::Optional<Box<T>> bounds = TransformedBounds(Box<T>{-HalfExtents, HalfExtents}, AffineTransform<T>{linear, Center});
		ASSERT(bounds.has_value(), "Geometry coordinate must fit the finite scalar range.");
		return *bounds;
	}

	template <MathScalar T>
	bool OrientedBox<T>::Contains(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Oriented box must be valid.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const detail::GeometryVector<T> local = detail::VolumeLocal<T>(detail::VolumeRotation<T>(Rotation), detail::GeometryVector<T>(point) - detail::GeometryVector<T>(Center));
		return core::Abs(local.X) <= HalfExtents.X && core::Abs(local.Y) <= HalfExtents.Y && core::Abs(local.Z) <= HalfExtents.Z;
	}

	template <MathScalar T>
	Vector3<T> OrientedBox<T>::ClosestPoint(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Oriented box must be valid.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const detail::VolumeFrame<T> frame = detail::VolumeRotation<T>(Rotation);
		const detail::GeometryVector<T> local = detail::VolumeLocal<T>(frame, detail::GeometryVector<T>(point) - detail::GeometryVector<T>(Center));
		const detail::GeometryVector<T> clamped{
		    core::ClampValue(local.X, -static_cast<T>(HalfExtents.X), static_cast<T>(HalfExtents.X)),
		    core::ClampValue(local.Y, -static_cast<T>(HalfExtents.Y), static_cast<T>(HalfExtents.Y)),
		    core::ClampValue(local.Z, -static_cast<T>(HalfExtents.Z), static_cast<T>(HalfExtents.Z))};
		if (clamped.X == local.X && clamped.Y == local.Y && clamped.Z == local.Z)
		{
			return point;
		}

		return detail::GeometryPoint<T>(detail::GeometryVector<T>(Center) + detail::VolumeWorld<T>(frame, clamped));
	}

	template <MathScalar T>
	T OrientedBox<T>::DistanceSquared(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Oriented box must be valid.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const detail::GeometryVector<T> local = detail::VolumeLocal<T>(detail::VolumeRotation<T>(Rotation), detail::GeometryVector<T>(point) - detail::GeometryVector<T>(Center));
		const detail::GeometryVector<T> gap{core::Max(T(0.0), core::Abs(local.X) - HalfExtents.X),
			core::Max(T(0.0), core::Abs(local.Y) - HalfExtents.Y),
			core::Max(T(0.0), core::Abs(local.Z) - HalfExtents.Z)};
		return detail::GeometryMagnitude<T>(gap.LengthSquared());
	}

	template struct OrientedBox<float32>;
	template struct OrientedBox<float64>;
} // namespace rox::math
