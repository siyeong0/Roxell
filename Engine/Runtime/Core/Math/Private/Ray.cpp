#include <Ray.h>
#include <Assertion.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rox::math
{
	template <MathScalar T>
	static T geometryRayCoordinate(T value) noexcept
	{
		ASSERT(rox::core::IsFinite(value), "Geometry coordinate must fit the finite scalar range.");
		return static_cast<T>(value);
	}

	template <MathScalar T>
	struct RayProjection
	{
		Vector3<T> Direction;
		T DirectionScale;
		T PositionScale;
		T Parameter;
	};

	template <MathScalar T>
	static RayProjection<T> geometryRayProjection(const Ray<T>& ray, const Vector3<T>& point) noexcept
	{
		const T positionScale = rox::core::Max({point.Abs().X, point.Abs().Y, point.Abs().Z, ray.Origin.Abs().X, ray.Origin.Abs().Y, ray.Origin.Abs().Z});
		const T directionScale = rox::core::Max({rox::core::Abs(ray.Direction.X), rox::core::Abs(ray.Direction.Y), rox::core::Abs(ray.Direction.Z)});
		const Vector3<T> direction = ray.Direction / directionScale;
		if (positionScale == T(0))
		{
			return {direction, directionScale, positionScale, T(0)};
		}

		const Vector3<T> offset = point / positionScale - ray.Origin / positionScale;
		const T projected = rox::core::Max(T(0), offset.Dot(direction) / direction.Dot(direction));
		return {direction, directionScale, positionScale, projected};
	}

	template <MathScalar T>
	static Vector3<T> geometryRayPoint(const Ray<T>& ray, const Vector3<T>& point) noexcept
	{
		const RayProjection<T> projection = geometryRayProjection(ray, point);
		// The ray parameter may overflow even when every coordinate of its projection is representable.
		return {rox::core::Fma(projection.Parameter * projection.Direction.X, projection.PositionScale, ray.Origin.X),
		        rox::core::Fma(projection.Parameter * projection.Direction.Y, projection.PositionScale, ray.Origin.Y),
		        rox::core::Fma(projection.Parameter * projection.Direction.Z, projection.PositionScale, ray.Origin.Z)};
	}

	template <MathScalar T>
	bool Ray<T>::IsValid() const noexcept
	{
		return Origin.IsFinite() && Direction.IsFinite() && (Direction.X != T(0.0) || Direction.Y != T(0.0) || Direction.Z != T(0.0));
	}

	template <MathScalar T>
	Vector3<T> Ray<T>::At(T parameter) const noexcept
	{
		ASSERT(IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(rox::core::IsFinite(parameter) && parameter >= T(0.0), "Ray parameter must be finite and non-negative.");
		return {geometryRayCoordinate<T>(rox::core::Fma(parameter, Direction.X, Origin.X)),
		        geometryRayCoordinate<T>(rox::core::Fma(parameter, Direction.Y, Origin.Y)),
		        geometryRayCoordinate<T>(rox::core::Fma(parameter, Direction.Z, Origin.Z))};
	}

	template <MathScalar T>
	T Ray<T>::ClosestParameter(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const RayProjection<T> projection = geometryRayProjection(*this, point);
		if (projection.Parameter == T(0))
		{
			return T(0);
		}

		int parameterExponent = 0;
		int positionExponent = 0;
		int directionExponent = 0;
		const T parameter = std::frexp(projection.Parameter, &parameterExponent);
		const T position = std::frexp(projection.PositionScale, &positionExponent);
		const T direction = std::frexp(projection.DirectionScale, &directionExponent);
		return rox::core::Min(
		    std::scalbn(parameter * position / direction, parameterExponent + positionExponent - directionExponent),
		    rox::core::NumericLimits<T>::max());
	}

	template <MathScalar T>
	Vector3<T> Ray<T>::ClosestPoint(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const Vector3<T> closest = geometryRayPoint(*this, point);
		return {geometryRayCoordinate(closest.X), geometryRayCoordinate(closest.Y), geometryRayCoordinate(closest.Z)};
	}

	template <MathScalar T>
	T Ray<T>::DistanceSquared(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const Vector3<T> closest = geometryRayPoint(*this, point);
		const T x = point.X - closest.X;
		const T y = point.Y - closest.Y;
		const T z = point.Z - closest.Z;
		const T squared = x * x + y * y + z * z;
		return squared > rox::core::NumericLimits<T>::max() ? rox::core::NumericLimits<T>::infinity()
		                                                    : static_cast<T>(squared);
	}

	template struct Ray<float32>;
	template struct Ray<float64>;
} // namespace rox::math
