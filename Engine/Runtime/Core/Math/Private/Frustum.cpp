#include <Frustum.h>
#include <Numeric.hpp>
#include <Comparison.hpp>

#include <cmath>

namespace rox::math
{
	namespace
	{
		constexpr EFrustumPlane FRUSTUM_PLANES[] = {EFrustumPlane::Left, EFrustumPlane::Right, EFrustumPlane::Bottom, EFrustumPlane::Top,  EFrustumPlane::Near,  EFrustumPlane::Far};

		template <MathScalar T>
		[[nodiscard]] T planeNormalLength(const Plane<T>& plane) noexcept
		{
			return rox::core::Hypot(static_cast<T>(plane.Normal.X), static_cast<T>(plane.Normal.Y), static_cast<T>(plane.Normal.Z));
		}

		template <MathScalar T>
		[[nodiscard]] T signedDistance(const Plane<T>& plane, const Vector3<T>& point) noexcept
		{
			return static_cast<T>(plane.Normal.X) * static_cast<T>(point.X) +
			       static_cast<T>(plane.Normal.Y) * static_cast<T>(point.Y) +
			       static_cast<T>(plane.Normal.Z) * static_cast<T>(point.Z) + static_cast<T>(plane.Distance);
		}
	} // namespace

	template <MathScalar T>
	Frustum<T> Frustum<T>::FromViewProjection(const Matrix4x4<T>& matrix) noexcept
	{
		// Row-vector, depth [0, 1]: side planes use column 3 +/- column axis; near uses column 2.
		Frustum<T> result;
		result.Left = Plane<T>({matrix.M[0][3] + matrix.M[0][0], matrix.M[1][3] + matrix.M[1][0], matrix.M[2][3] + matrix.M[2][0]}, matrix.M[3][3] + matrix.M[3][0]);
		result.Right = Plane<T>({matrix.M[0][3] - matrix.M[0][0], matrix.M[1][3] - matrix.M[1][0], matrix.M[2][3] - matrix.M[2][0]}, matrix.M[3][3] - matrix.M[3][0]);
		result.Bottom = Plane<T>({matrix.M[0][3] + matrix.M[0][1], matrix.M[1][3] + matrix.M[1][1], matrix.M[2][3] + matrix.M[2][1]}, matrix.M[3][3] + matrix.M[3][1]);
		result.Top = Plane<T>({matrix.M[0][3] - matrix.M[0][1], matrix.M[1][3] - matrix.M[1][1], matrix.M[2][3] - matrix.M[2][1]}, matrix.M[3][3] - matrix.M[3][1]);
		result.Near = Plane<T>({matrix.M[0][2], matrix.M[1][2], matrix.M[2][2]}, matrix.M[3][2]);
		result.Far = Plane<T>({matrix.M[0][3] - matrix.M[0][2], matrix.M[1][3] - matrix.M[1][2], matrix.M[2][3] - matrix.M[2][2]}, matrix.M[3][3] - matrix.M[3][2]);

		for (const EFrustumPlane index : FRUSTUM_PLANES)
		{
			Plane<T>& plane = result.GetPlane(index);
			const bool unbounded = (index == EFrustumPlane::Near || index == EFrustumPlane::Far) &&
			                       plane.Normal == Vector3<T>::Zero() && rox::core::IsFinite(plane.Distance) &&
			                       plane.Distance > T(0.0);

			if (unbounded)
			{
				plane.Distance = T(1.0);
			}
			else
			{
				plane = plane.Normalized(T(0.0));
			}
		}

		return result;
	}

	template <MathScalar T>
	const Plane<T>& Frustum<T>::GetPlane(EFrustumPlane plane) const noexcept
	{
		ASSERT(plane != EFrustumPlane::Count, "Frustum plane must identify a concrete plane.");

		switch (plane)
		{
		case EFrustumPlane::Left:
			return Left;
		case EFrustumPlane::Right:
			return Right;
		case EFrustumPlane::Bottom:
			return Bottom;
		case EFrustumPlane::Top:
			return Top;
		case EFrustumPlane::Near:
			return Near;
		case EFrustumPlane::Far:
			return Far;
		case EFrustumPlane::Count:
			break;
		default:
			break;
		}

		ASSERT(false, "Frustum plane value is out of range.");
	}

	template <MathScalar T>
	Plane<T>& Frustum<T>::GetPlane(EFrustumPlane plane) noexcept
	{
		ASSERT(plane != EFrustumPlane::Count, "Frustum plane must identify a concrete plane.");

		switch (plane)
		{
		case EFrustumPlane::Left:
			return Left;
		case EFrustumPlane::Right:
			return Right;
		case EFrustumPlane::Bottom:
			return Bottom;
		case EFrustumPlane::Top:
			return Top;
		case EFrustumPlane::Near:
			return Near;
		case EFrustumPlane::Far:
			return Far;
		case EFrustumPlane::Count:
			break;
		default:
			break;
		}

		ASSERT(false, "Frustum plane value is out of range.");
	}

	template <MathScalar T>
	bool Frustum<T>::IsValid(T epsilon) const noexcept
	{
		ASSERT(rox::core::IsFinite(epsilon) && epsilon >= T(0.0), "Frustum validation epsilon must be finite and non-negative.");

		for (const EFrustumPlane plane : FRUSTUM_PLANES)
		{
			const Plane<T>& boundary = GetPlane(plane);

			if ((plane == EFrustumPlane::Near || plane == EFrustumPlane::Far) &&
			    boundary.Normal == Vector3<T>::Zero() && rox::core::IsFinite(boundary.Distance) &&
			    boundary.Distance > T(0.0))
			{
				continue;
			}

			const Vector3<T>& normal = boundary.Normal;
			if (!normal.IsFinite() || !core::IsFinite(boundary.Distance))
			{
				return false;
			}

			const T scale = core::Max({core::Abs(normal.X), core::Abs(normal.Y), core::Abs(normal.Z)});
			if (scale == T(0))
			{
				return false;
			}

			// A component above the threshold proves the length exceeds it without normalization.
			if (scale <= epsilon && scale <= epsilon / core::Hypot(normal.X / scale, normal.Y / scale, normal.Z / scale))
			{
				return false;
			}
		}

		return true;
	}

	template <MathScalar T>
	bool Frustum<T>::ContainsPoint(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Frustum must contain valid half-spaces.");
		ASSERT(point.IsFinite(), "Point must be finite.");

		for (const EFrustumPlane plane : FRUSTUM_PLANES)
		{
			if (signedDistance<T>(GetPlane(plane), point) < T(0.0))
			{
				return false;
			}
		}

		return true;
	}

	template <MathScalar T>
	bool Frustum<T>::Intersects(const Sphere<T>& sphere) const noexcept
	{
		ASSERT(IsValid(), "Frustum must contain valid half-spaces.");
		ASSERT(sphere.IsValid(), "Sphere must be valid.");

		for (const EFrustumPlane plane : FRUSTUM_PLANES)
		{
			const Plane<T>& boundary = GetPlane(plane);
			const T normalLength = planeNormalLength<T>(boundary);

			if (signedDistance<T>(boundary, sphere.Center) < -static_cast<T>(sphere.Radius) * normalLength)
			{
				return false;
			}
		}

		return true;
	}

	template <MathScalar T>
	bool Frustum<T>::Intersects(const Box<T>& box) const noexcept
	{
		return Classify(box) != EFrustumContainment::Outside;
	}

	template <MathScalar T>
	EFrustumContainment Frustum<T>::Classify(const Box<T>& box) const noexcept
	{
		ASSERT(IsValid(), "Frustum must contain valid half-spaces.");
		ASSERT(box.IsValid(), "Box must be valid.");
		const Vector3<T> center{core::LerpValue(box.Min.X, box.Max.X, T(0.5)),
		                        core::LerpValue(box.Min.Y, box.Max.Y, T(0.5)),
		                        core::LerpValue(box.Min.Z, box.Max.Z, T(0.5))};
		const Vector3<T> extents = box.Max * T(0.5) - box.Min * T(0.5);
		EFrustumContainment result = EFrustumContainment::Inside;

		for (const EFrustumPlane plane : FRUSTUM_PLANES)
		{
			const Plane<T>& boundary = GetPlane(plane);
			const T projectedRadius = rox::core::Abs(static_cast<T>(boundary.Normal.X)) * extents.X +
			                          rox::core::Abs(static_cast<T>(boundary.Normal.Y)) * extents.Y +
			                          rox::core::Abs(static_cast<T>(boundary.Normal.Z)) * extents.Z;
			const T distance = signedDistance<T>(boundary, center);

			if (distance < -projectedRadius)
			{
				return EFrustumContainment::Outside;
			}

			if (distance <= projectedRadius)
			{
				result = EFrustumContainment::Intersecting;
			}
		}

		return result;
	}

	template struct Frustum<float32>;
	template struct Frustum<float64>;
} // namespace rox::math
