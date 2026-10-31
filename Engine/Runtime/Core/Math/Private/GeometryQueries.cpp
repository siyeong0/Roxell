#include <GeometryQueries.h>
#include "GeometryDetail.hpp"
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

#include <algorithm>

namespace rox::math
{
	template <MathScalar T>
	struct GeometrySphereExpansion final
	{
		T Values[128]{};
		uint32 Count = 0;

		void AddProduct(T a, T b) noexcept
		{
			const T value = a * b;
			Add(value);
			Add(rox::core::Fma(a, b, -value));
		}

		void Add(T value) noexcept
		{
			uint32 retained = 0;

			for (uint32 piece = 0; piece < Count; ++piece)
			{
				const T addend = Values[piece];
				const T sum = value + addend;
				const T virtualAddend = sum - value;
				const T error = (value - (sum - virtualAddend)) + (addend - virtualAddend);

				if (error != 0)
				{
					Values[retained++] = error;
				}

				value = sum;
			}

			if (value != 0 || retained == 0)
			{
				Values[retained++] = value;
			}

			Count = retained;
		}
	};

	template <MathScalar T>
	static T geometrySphereDiscriminant(const Ray<T>& ray, const Sphere<T>& sphere, const detail::GeometryVector<T>& offset,
	                                    const detail::GeometryVector<T>& direction, T directionSquared) noexcept
	{
		const T radiusSquared = static_cast<T>(sphere.Radius) * sphere.Radius;
		const T approximate = radiusSquared * directionSquared - offset.Cross(direction).LengthSquared();
		const detail::GeometryVector<T> magnitude{
		    rox::core::Abs(static_cast<T>(ray.Origin.X)) + rox::core::Abs(static_cast<T>(sphere.Center.X)),
		    rox::core::Abs(static_cast<T>(ray.Origin.Y)) + rox::core::Abs(static_cast<T>(sphere.Center.Y)),
		    rox::core::Abs(static_cast<T>(ray.Origin.Z)) + rox::core::Abs(static_cast<T>(sphere.Center.Z))};
		const T errorBound = 128 * rox::core::NumericLimits<T>::epsilon() * (radiusSquared + magnitude.LengthSquared()) * directionSquared;

		if (rox::core::Abs(approximate) > errorBound)
		{
			return approximate;
		}

		const T origins[]{ray.Origin.X, ray.Origin.Y, ray.Origin.Z};
		const T centers[]{sphere.Center.X, sphere.Center.Y, sphere.Center.Z};
		const T directions[]{ray.Direction.X, ray.Direction.Y, ray.Direction.Z};
		GeometrySphereExpansion<T> discriminant;

		for (uint32 axis = 0; axis < 3; ++axis)
		{
			const T product = sphere.Radius * directions[axis];
			const T terms[]{product, rox::core::Fma(sphere.Radius, directions[axis], -product)};
			for (T left : terms)
			{
				for (T right : terms)
				{
					discriminant.AddProduct(left, right);
				}
			}

			const uint32 first = (axis + 1) % 3;
			const uint32 second = (axis + 2) % 3;
			GeometrySphereExpansion<T> cross;
			cross.AddProduct(origins[first], directions[second]);
			cross.AddProduct(-centers[first], directions[second]);
			cross.AddProduct(-origins[second], directions[first]);
			cross.AddProduct(centers[second], directions[first]);

			for (uint32 left = 0; left < cross.Count; ++left)
			{
				for (uint32 right = 0; right < cross.Count; ++right)
				{
					const T squaredTerm = cross.Values[left] * cross.Values[right];
					discriminant.Add(-squaredTerm);
					discriminant.Add(-rox::core::Fma(cross.Values[left], cross.Values[right], -squaredTerm));
				}
			}
		}

		return discriminant.Values[discriminant.Count - 1];
	}

	template <MathScalar T>
	static bool geometryPlaneValid(const Plane<T>& plane) noexcept
	{
		return plane.Normal.IsFinite() && rox::core::IsFinite(plane.Distance) && (plane.Normal.X != 0 || plane.Normal.Y != 0 || plane.Normal.Z != 0);
	}

	template <MathScalar T>
	static detail::GeometryVector<T> geometryClosest(const Vector3<T>& point, const Plane<T>& plane) noexcept
	{
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		ASSERT(core::IsFinite(plane.Distance), "Geometry plane must have finite coefficients and a nonzero normal.");
		detail::GeometryVector<T> normal(plane.Normal);
		const detail::GeometryVector<T> query(point);
		T squared = normal.LengthSquared();
		T distance = plane.Distance;
		if (!core::IsFinite(squared) || squared < core::NumericLimits<T>::min())
		{
			ASSERT(geometryPlaneValid<T>(plane), "Geometry plane must have finite coefficients and a nonzero normal.");
			const T scale = normal.MaxAbs();
			normal = normal / scale;
			distance /= scale;
			squared = normal.LengthSquared();
		}

		return query - normal * ((normal.Dot(query) + distance) / squared);
	}

	template <MathScalar T>
	bool Intersect(const Ray<T>& ray, const Plane<T>& plane, T* outParameter, T maxParameter) noexcept
	{
		ASSERT(outParameter != nullptr, "Intersect requires a valid output pointer.");
		ASSERT(ray.IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(plane.Normal.X != 0 || plane.Normal.Y != 0 || plane.Normal.Z != 0, "Geometry plane must have finite coefficients and a nonzero normal.");
		ASSERT(rox::core::IsFinite(maxParameter) && maxParameter >= 0, "Ray query limit must be finite and non-negative.");

		detail::GeometryVector<T> normal(plane.Normal);
		T denominator = normal.Dot(detail::GeometryVector<T>(ray.Direction));
		T distance = normal.Dot(detail::GeometryVector<T>(ray.Origin)) + plane.Distance;
		if (!core::IsFinite(denominator) || !core::IsFinite(distance))
		{
			ASSERT(geometryPlaneValid<T>(plane), "Geometry plane must have finite coefficients and a nonzero normal.");
			const T scale = normal.MaxAbs();
			normal = normal / scale;
			denominator = normal.Dot(detail::GeometryVector<T>(ray.Direction));
			distance = normal.Dot(detail::GeometryVector<T>(ray.Origin)) + plane.Distance / scale;
		}

		if (denominator == 0 && distance != 0)
		{
			return false;
		}

		const T parameter = denominator == 0 ? 0 : -distance / denominator;

		if (!(parameter >= 0 && parameter <= maxParameter))
		{
			return false;
		}

		(*outParameter) = static_cast<T>(parameter);
		return true;
	}

	template <MathScalar T>
	static detail::GeometryVector<T> geometryClosest(const Vector3<T>& point, const Box<T>& box) noexcept
	{
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		ASSERT(box.IsValid(), "Geometry box must be valid.");
		return {rox::core::ClampValue(point.X, box.Min.X, box.Max.X),
		        rox::core::ClampValue(point.Y, box.Min.Y, box.Max.Y),
		        rox::core::ClampValue(point.Z, box.Min.Z, box.Max.Z)};
	}

	template <MathScalar T>
	bool Intersect(const Ray<T>& ray, const Box<T>& box, RayInterval<T>* outInterval, T maxParameter) noexcept
	{
		ASSERT(outInterval != nullptr, "Intersect requires a valid output pointer.");
		ASSERT(ray.IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(box.IsValid(), "Geometry box must be valid.");
		ASSERT(rox::core::IsFinite(maxParameter) && maxParameter >= 0, "Ray query limit must be finite and non-negative.");

		const T origins[]{ray.Origin.X, ray.Origin.Y, ray.Origin.Z};
		const T directions[]{ray.Direction.X, ray.Direction.Y, ray.Direction.Z};
		const T lower[]{box.Min.X, box.Min.Y, box.Min.Z};
		const T upper[]{box.Max.X, box.Max.Y, box.Max.Z};
		T enter = 0;
		T exit = maxParameter;

		for (uint32 axis = 0; axis < 3; ++axis)
		{
			if (directions[axis] == 0)
			{
				if (origins[axis] < lower[axis] || origins[axis] > upper[axis])
				{
					return false;
				}

				continue;
			}

			const T first = (lower[axis] - origins[axis]) / directions[axis];
			const T second = (upper[axis] - origins[axis]) / directions[axis];
			enter = rox::core::Max(enter, rox::core::Min(first, second));
			exit = rox::core::Min(exit, rox::core::Max(first, second));

			if (enter > exit)
			{
				return false;
			}
		}

		(*outInterval) = {static_cast<T>(enter), static_cast<T>(exit)};
		return true;
	}

	template <MathScalar T>
	static detail::GeometryVector<T> geometryClosest(const Vector3<T>& point, const Sphere<T>& sphere) noexcept
	{
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		ASSERT(sphere.IsValid(), "Geometry sphere must be valid.");
		const detail::GeometryVector<T> center(sphere.Center), query(point);
		const detail::GeometryVector<T> offset = query - center;
		const T distance = rox::core::Hypot(offset.X, offset.Y, offset.Z);

		if (distance <= sphere.Radius)
		{
			return query;
		}

		return center + offset * (sphere.Radius / distance);
	}

	template <MathScalar T>
	Vector3<T> ClosestPoint(const Vector3<T>& point, const Plane<T>& plane) noexcept
	{
		return detail::GeometryPoint<T>(geometryClosest<T>(point, plane));
	}

	template <MathScalar T>
	bool Intersect(const Ray<T>& ray, const Triangle<T>& triangle, T* outParameter, T maxParameter) noexcept
	{
		ASSERT(outParameter != nullptr, "Intersect requires a valid output pointer.");
		ASSERT(ray.IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(triangle.IsValid(), "Triangle vertices must be finite.");
		ASSERT(rox::core::IsFinite(maxParameter) && maxParameter >= 0, "Ray query limit must be finite and non-negative.");

		const detail::GeometryVector<T> rawAb = detail::GeometryVector<T>(triangle.B) - detail::GeometryVector<T>(triangle.A);
		const detail::GeometryVector<T> rawAc = detail::GeometryVector<T>(triangle.C) - detail::GeometryVector<T>(triangle.A);
		const T scale = rox::core::Max({rawAb.MaxAbs(), rawAc.MaxAbs(), detail::GeometryVector<T>(ray.Direction).MaxAbs()});
		if (scale == T(0) || !rox::core::IsFinite(scale))
		{
			return false;
		}

		const detail::GeometryVector<T> ab = rawAb / scale;
		const detail::GeometryVector<T> ac = rawAc / scale;
		const detail::GeometryVector<T> direction = detail::GeometryVector<T>(ray.Direction) / scale;
		const detail::GeometryVector<T> perpendicular = direction.Cross(ac);
		const T determinant = ab.Dot(perpendicular);

		if (determinant == 0)
		{
			return false;
		}

		const detail::GeometryVector<T> offset = (detail::GeometryVector<T>(ray.Origin) - detail::GeometryVector<T>(triangle.A)) / scale;
		const T b = offset.Dot(perpendicular) / determinant;

		if (b < 0 || b > 1)
		{
			return false;
		}

		const detail::GeometryVector<T> crossed = offset.Cross(ab);
		const T c = direction.Dot(crossed) / determinant;

		if (c < 0 || b + c > 1)
		{
			return false;
		}

		const T parameter = ac.Dot(crossed) / determinant;

		if (!(parameter >= 0 && parameter <= maxParameter))
		{
			return false;
		}

		(*outParameter) = static_cast<T>(parameter);
		return true;
	}

	template <MathScalar T>
	bool Intersect(const Ray<T>& ray, const Sphere<T>& sphere, RayInterval<T>* outInterval, T maxParameter) noexcept
	{
		ASSERT(outInterval != nullptr, "Intersect requires a valid output pointer.");
		ASSERT(ray.IsValid(), "Ray must have finite origin and a finite nonzero direction.");
		ASSERT(sphere.IsValid(), "Geometry sphere must be valid.");
		ASSERT(rox::core::IsFinite(maxParameter) && maxParameter >= 0, "Ray query limit must be finite and non-negative.");
		const T magnitude = core::Max({detail::GeometryVector<T>(ray.Origin).MaxAbs(),
		                               detail::GeometryVector<T>(ray.Direction).MaxAbs(),
		                               detail::GeometryVector<T>(sphere.Center).MaxAbs(), sphere.Radius});
		int exponent = 0;
		std::frexp(magnitude, &exponent);
		const auto scaled = [exponent](const Vector3<T>& value)
		{
			return Vector3<T>{std::scalbn(value.X, -exponent), std::scalbn(value.Y, -exponent), std::scalbn(value.Z, -exponent)};
		};
		const Ray<T> localRay{scaled(ray.Origin), scaled(ray.Direction)};
		const Sphere<T> localSphere{scaled(sphere.Center), std::scalbn(sphere.Radius, -exponent)};
		const detail::GeometryVector<T> offset = detail::GeometryVector<T>(localRay.Origin) - detail::GeometryVector<T>(localSphere.Center);
		const detail::GeometryVector<T> direction(localRay.Direction);
		const T directionSquared = direction.LengthSquared();
		if (directionSquared == T(0))
		{
			return false;
		}

		const T radius = localSphere.Radius;
		const T discriminant = geometrySphereDiscriminant<T>(localRay, localSphere, offset, direction, directionSquared);
		if (discriminant < 0)
		{
			return false;
		}

		const T center = -offset.Dot(direction) / directionSquared;
		const T halfWidth = rox::core::Sqrt(discriminant) / directionSquared;
		const T stableRoot = center + rox::core::CopySign(halfWidth, center);
		const T length = rox::core::Hypot(offset.X, offset.Y, offset.Z);
		const T otherRoot = discriminant == T(0) ? center
		                    : stableRoot == 0    ? 0
		                                      : ((length - radius) * (length + radius) / directionSquared) / stableRoot;
		const T enter = rox::core::Max(T(0.0), rox::core::Min(stableRoot, otherRoot));
		const T exit = rox::core::Min(static_cast<T>(maxParameter), rox::core::Max(stableRoot, otherRoot));

		if (enter > exit)
		{
			return false;
		}

		(*outInterval) = {static_cast<T>(enter), static_cast<T>(exit)};
		return true;
	}

	template <MathScalar T>
	Vector3<T> ClosestPoint(const Vector3<T>& point, const Box<T>& box) noexcept
	{
		return detail::GeometryPoint<T>(geometryClosest<T>(point, box));
	}

	template <MathScalar T>
	Vector3<T> ClosestPoint(const Vector3<T>& point, const Sphere<T>& sphere) noexcept
	{
		return detail::GeometryPoint<T>(geometryClosest<T>(point, sphere));
	}

	template <MathScalar T>
	T DistanceSquared(const Vector3<T>& point, const Plane<T>& plane) noexcept
	{
		const detail::GeometryVector<T> closest = geometryClosest<T>(point, plane);
		return detail::GeometryMagnitude<T>((detail::GeometryVector<T>(point) - closest).LengthSquared());
	}

	template <MathScalar T>
	T DistanceSquared(const Vector3<T>& point, const Box<T>& box) noexcept
	{
		const detail::GeometryVector<T> closest = geometryClosest<T>(point, box);
		return detail::GeometryMagnitude<T>((detail::GeometryVector<T>(point) - closest).LengthSquared());
	}

	template <MathScalar T>
	T DistanceSquared(const Vector3<T>& point, const Sphere<T>& sphere) noexcept
	{
		const detail::GeometryVector<T> closest = geometryClosest<T>(point, sphere);
		return detail::GeometryMagnitude<T>((detail::GeometryVector<T>(point) - closest).LengthSquared());
	}

	template bool Intersect(const Ray<float32>& ray, const Plane<float32>& plane, float32* outParameter, float32 maxParameter) noexcept;
	template bool Intersect(const Ray<float64>& ray, const Plane<float64>& plane, float64* outParameter, float64 maxParameter) noexcept;
	template bool Intersect(const Ray<float32>& ray, const Box<float32>& box, RayInterval<float32>* outInterval, float32 maxParameter) noexcept;
	template bool Intersect(const Ray<float64>& ray, const Box<float64>& box, RayInterval<float64>* outInterval, float64 maxParameter) noexcept;
	template Vector3<float32> ClosestPoint(const Vector3<float32>& point, const Plane<float32>& plane) noexcept;
	template Vector3<float64> ClosestPoint(const Vector3<float64>& point, const Plane<float64>& plane) noexcept;
	template bool Intersect(const Ray<float32>& ray, const Triangle<float32>& triangle, float32* outParameter, float32 maxParameter) noexcept;
	template bool Intersect(const Ray<float64>& ray, const Triangle<float64>& triangle, float64* outParameter, float64 maxParameter) noexcept;
	template bool Intersect(const Ray<float32>& ray, const Sphere<float32>& sphere, RayInterval<float32>* outInterval, float32 maxParameter) noexcept;
	template bool Intersect(const Ray<float64>& ray, const Sphere<float64>& sphere, RayInterval<float64>* outInterval, float64 maxParameter) noexcept;
	template Vector3<float32> ClosestPoint(const Vector3<float32>& point, const Box<float32>& box) noexcept;
	template Vector3<float64> ClosestPoint(const Vector3<float64>& point, const Box<float64>& box) noexcept;
	template Vector3<float32> ClosestPoint(const Vector3<float32>& point, const Sphere<float32>& sphere) noexcept;
	template Vector3<float64> ClosestPoint(const Vector3<float64>& point, const Sphere<float64>& sphere) noexcept;
	template float32 DistanceSquared(const Vector3<float32>& point, const Plane<float32>& plane) noexcept;
	template float64 DistanceSquared(const Vector3<float64>& point, const Plane<float64>& plane) noexcept;
	template float32 DistanceSquared(const Vector3<float32>& point, const Box<float32>& box) noexcept;
	template float64 DistanceSquared(const Vector3<float64>& point, const Box<float64>& box) noexcept;
	template float32 DistanceSquared(const Vector3<float32>& point, const Sphere<float32>& sphere) noexcept;
	template float64 DistanceSquared(const Vector3<float64>& point, const Sphere<float64>& sphere) noexcept;
} // namespace rox::math
