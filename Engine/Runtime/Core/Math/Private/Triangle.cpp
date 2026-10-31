#include <Triangle.h>
#include "GeometryDetail.hpp"
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

#include <algorithm>

namespace rox::math
{
	template <MathScalar T>
	static detail::GeometryVector<T> geometryTriangleCross(const Triangle<T>& triangle) noexcept
	{
		return (detail::GeometryVector<T>(triangle.B) - detail::GeometryVector<T>(triangle.A)).Cross(detail::GeometryVector<T>(triangle.C) - detail::GeometryVector<T>(triangle.A));
	}

	template <MathScalar T>
	bool Triangle<T>::IsValid() const noexcept
	{
		return A.IsFinite() && B.IsFinite() && C.IsFinite();
	}

	template <MathScalar T>
	static bool geometryTriangleWeights(const Triangle<T>& triangle, const Vector3<T>& point, detail::GeometryVector<T>& outWeights) noexcept
	{
		const detail::GeometryVector<T> rawAb = detail::GeometryVector<T>(triangle.B) - detail::GeometryVector<T>(triangle.A);
		const detail::GeometryVector<T> rawAc = detail::GeometryVector<T>(triangle.C) - detail::GeometryVector<T>(triangle.A);
		const T scale = rox::core::Max(rawAb.MaxAbs(), rawAc.MaxAbs());
		if (scale == T(0) || !rox::core::IsFinite(scale))
		{
			return false;
		}

		const detail::GeometryVector<T> ab = rawAb / scale;
		const detail::GeometryVector<T> ac = rawAc / scale;
		const detail::GeometryVector<T> ap = (detail::GeometryVector<T>(point) - detail::GeometryVector<T>(triangle.A)) / scale;
		const detail::GeometryVector<T> normal = ab.Cross(ac);
		const T squared = normal.LengthSquared();

		if (squared == 0)
		{
			return false;
		}

		const T b = ap.Cross(ac).Dot(normal) / squared;
		const T c = ab.Cross(ap).Dot(normal) / squared;
		outWeights = {1 - b - c, b, c};
		return true;
	}

	template <MathScalar T>
	bool Triangle<T>::Barycentric(const Vector3<T>& point, Vector3<T>* outWeights) const noexcept
	{
		ASSERT(outWeights != nullptr, "Barycentric requires a valid output pointer.");
		ASSERT(IsValid(), "Triangle vertices must be finite.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		detail::GeometryVector<T> weights{0, 0, 0};
		if (!geometryTriangleWeights<T>(*this, point, weights))
		{
			return false;
		}

		const Vector3<T> result = detail::GeometryPoint<T>(weights);
		(*outWeights) = result;
		return true;
	}

	template <MathScalar T>
	static detail::GeometryVector<T> geometryTriangleClosest(const Triangle<T>& triangle, const Vector3<T>& point) noexcept
	{
		const detail::GeometryVector<T> vertices[]{detail::GeometryVector<T>(triangle.A), detail::GeometryVector<T>(triangle.B), detail::GeometryVector<T>(triangle.C)};
		const detail::GeometryVector<T> query(point);
		detail::GeometryVector<T> weights{0, 0, 0};
		if (geometryTriangleWeights<T>(triangle, point, weights) && weights.X >= 0 && weights.Y >= 0 && weights.Z >= 0)
		{
			return vertices[0] * weights.X + vertices[1] * weights.Y + vertices[2] * weights.Z;
		}

		detail::GeometryVector<T> closest = vertices[0];
		T nearest = rox::core::NumericLimits<T>::infinity();

		for (uint32 edge = 0; edge < 3; ++edge)
		{
			const detail::GeometryVector<T> start = vertices[edge];
			const detail::GeometryVector<T> end = vertices[(edge + 1) % 3];
			const detail::GeometryVector<T> rawDirection = end - start;
			const T scale = rawDirection.MaxAbs();
			const detail::GeometryVector<T> direction = scale == T(0) ? rawDirection : rawDirection / scale;
			const T lengthSquared = direction.LengthSquared();
			const T parameter =
			    lengthSquared == 0
			        ? 0
			        : rox::core::ClampValue(((query - start) / scale).Dot(direction) / lengthSquared, T(0.0), T(1.0));
			const detail::GeometryVector<T> candidate = start * (1 - parameter) + end * parameter;
			const T distance = (query - candidate).Length();

			if (distance < nearest)
			{
				nearest = distance;
				closest = candidate;
			}
		}

		return closest;
	}

	template <MathScalar T>
	Vector3<T> Triangle<T>::ClosestPoint(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Triangle vertices must be finite.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		return detail::GeometryPoint<T>(geometryTriangleClosest<T>(*this, point));
	}

	template <MathScalar T>
	T Triangle<T>::DistanceSquared(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Triangle vertices must be finite.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		return detail::GeometryMagnitude<T>((detail::GeometryVector<T>(point) - geometryTriangleClosest<T>(*this, point)).LengthSquared());
	}

	template <MathScalar T>
	bool Triangle<T>::IsDegenerate() const noexcept
	{
		ASSERT(IsValid(), "Triangle vertices must be finite.");
		const Vector3<T> ab = B - A;
		const Vector3<T> ac = C - A;
		if (ab == Vector3<T>::Zero() || ac == Vector3<T>::Zero())
		{
			return true;
		}

		return ab.Normalized(T(0)).Cross(ac.Normalized(T(0))) == Vector3<T>::Zero();
	}

	template <MathScalar T>
	T Triangle<T>::Area() const noexcept
	{
		ASSERT(IsValid(), "Triangle vertices must be finite.");
		const detail::GeometryVector<T> cross = geometryTriangleCross<T>(*this);
		return detail::GeometryMagnitude<T>(T(0.5) * rox::core::Hypot(cross.X, cross.Y, cross.Z));
	}

	template <MathScalar T>
	Vector3<T> Triangle<T>::Centroid() const noexcept
	{
		ASSERT(IsValid(), "Triangle vertices must be finite.");
		return Vector3<T>::Lerp(Vector3<T>::Lerp(A, B, T(0.5)), C, T(1) / T(3));
	}

	template <MathScalar T>
	Vector3<T> Triangle<T>::Normal() const noexcept
	{
		ASSERT(IsValid(), "Triangle vertices must be finite.");
		ASSERT(!IsDegenerate(), "Triangle normal requires a nondegenerate triangle.");
		return (B - A).Normalized(T(0)).Cross((C - A).Normalized(T(0))).Normalized(T(0));
	}

	template <MathScalar T>
	Box<T> Triangle<T>::GetBounds() const noexcept
	{
		ASSERT(IsValid(), "Triangle vertices must be finite.");
		return {{rox::core::Min({A.X, B.X, C.X}), rox::core::Min({A.Y, B.Y, C.Y}), rox::core::Min({A.Z, B.Z, C.Z})},
		        {rox::core::Max({A.X, B.X, C.X}), rox::core::Max({A.Y, B.Y, C.Y}), rox::core::Max({A.Z, B.Z, C.Z})}};
	}

	template struct Triangle<float32>;
	template struct Triangle<float64>;
} // namespace rox::math
