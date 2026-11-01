#pragma once

#include <MathScalar.hpp>

#include <Capsule.h>
#include <Frustum.h>
#include <GeometryQueries.h>
#include <LineSegment.h>
#include <NumericLimits.hpp>
#include <OrientedBox.h>
#include <Sphere.h>

namespace rox::math
{
	enum class EPlaneSide : uint8
	{
		Negative,
		Intersecting,
		Positive
	};

	// Plane classification requires finite coefficients, a nonzero normal and a valid volume.
	// Normals need not be unit length. Tangency is Intersecting; no tolerance is added.
	template <MathScalar T = float32>
	[[nodiscard]] EPlaneSide Classify(const Plane<T>& plane, const Sphere<T>& sphere) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] EPlaneSide Classify(const Plane<T>& plane, const Box<T>& box) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] EPlaneSide Classify(const Plane<T>& plane, const OrientedBox<T>& box) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] EPlaneSide Classify(const Plane<T>& plane, const Capsule<T>& capsule) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] EFrustumContainment Classify(const Frustum<T>& frustum, const Sphere<T>& sphere) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] EFrustumContainment Classify(const Frustum<T>& frustum, const OrientedBox<T>& box) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] EFrustumContainment Classify(const Frustum<T>& frustum, const Capsule<T>& capsule) noexcept;

	template <MathScalar T = float32>
	struct FrustumCorners final
	{
		Vector3<T> Points[8]{};
	};

	template <MathScalar T = float32>
	[[nodiscard]] bool GetCorners(const Frustum<T>& frustum, FrustumCorners<T>* outCorners) noexcept;

	template <MathScalar T = float32>
	struct SegmentClosestPoints final
	{
		Vector3<T> First = Vector3<T>::Zero();
		Vector3<T> Second = Vector3<T>::Zero();

		T FirstParameter = T(0.0);
		T SecondParameter = T(0.0);
	};

	template <MathScalar T = float32>
	[[nodiscard]] SegmentClosestPoints<T> ClosestPoints(const LineSegment<T>& first, const LineSegment<T>& second) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T DistanceSquared(const LineSegment<T>& first, const LineSegment<T>& second) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Overlaps(const Sphere<T>& sphere, const Box<T>& box) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Overlaps(const OrientedBox<T>& box, const Sphere<T>& sphere) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Overlaps(const OrientedBox<T>& first, const OrientedBox<T>& second) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Overlaps(const Capsule<T>& capsule, const Sphere<T>& sphere) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Overlaps(const Capsule<T>& first, const Capsule<T>& second) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T DistanceSquared(const Capsule<T>& first, const Capsule<T>& second) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Box<T> Union(const Box<T>& first, const Box<T>& second) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Intersection(const Box<T>& first, const Box<T>& second, Box<T>* outIntersection) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Box<T> Expanded(const Box<T>& box, const Vector3<T>& amount) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Intersect(const Ray<T>& ray, const OrientedBox<T>& box, RayInterval<T>* outInterval, T maxParameter = core::NumericLimits<T>::max()) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Intersect(const Ray<T>& ray, const Capsule<T>& capsule, RayInterval<T>* outInterval, T maxParameter = core::NumericLimits<T>::max()) noexcept;
} // namespace rox::math
