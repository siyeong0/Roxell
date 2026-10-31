#include <ProjectionMath.h>
#include <MathConstants.hpp>
#include <Assertion.h>
#include <Numeric.hpp>
#include "ProjectionDetail.hpp"
#include <Comparison.hpp>

#include <cmath>
#include <limits>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		T projectionCoefficient(T value) noexcept
		{
			ASSERT(rox::core::IsFinite(value), "Projection coefficients must be representable in the finite scalar range.");
			return static_cast<T>(value);
		}
	} // namespace

	template <MathScalar T>
	Matrix4x4<T> PerspectiveOffCenterLH(T left, T right, T bottom, T top, T nearPlane, T farPlane, EProjectionDepth depth) noexcept
	{
		ASSERT(rox::core::IsFinite(left) && rox::core::IsFinite(right) && rox::core::IsFinite(bottom) && rox::core::IsFinite(top) && left < right && bottom < top, "Perspective lateral bounds must be finite and ordered.");
		ASSERT(rox::core::IsFinite(nearPlane) && nearPlane > 0, "Perspective near plane must be finite and positive.");
		ASSERT(rox::core::IsFinite(farPlane) && farPlane > nearPlane, "Perspective far plane must be finite and farther than near.");
		ASSERT(depth == EProjectionDepth::Forward || depth == EProjectionDepth::Reversed, "Projection depth mode must be a listed value.");
		const T n = nearPlane;
		const T f = farPlane;
		const T width = static_cast<T>(right) - left;
		const T height = static_cast<T>(top) - bottom;
		const T x = projectionCoefficient<T>(2 * n / width);
		const T y = projectionCoefficient<T>(2 * n / height);
		ASSERT(x > 0 && y > 0, "Perspective scale coefficients must remain positive.");
		const T a = depth == EProjectionDepth::Forward ? f / (f - n) : -n / (f - n);
		const T b = depth == EProjectionDepth::Forward ? -n * f / (f - n) : n * f / (f - n);
		return {x,
		        0,
		        0,
		        0,
		        0,
		        y,
		        0,
		        0,
		        projectionCoefficient<T>(-(static_cast<T>(right) + left) / width),
		        projectionCoefficient<T>(-(static_cast<T>(top) + bottom) / height),
		        projectionCoefficient<T>(a),
		        1,
		        0,
		        0,
		        projectionCoefficient<T>(b),
		        0};
	}

	template <MathScalar T>
	Matrix4x4<T> PerspectiveOffCenterInfiniteLH(T left, T right, T bottom, T top, T nearPlane, EProjectionDepth depth) noexcept
	{
		ASSERT(rox::core::IsFinite(left) && rox::core::IsFinite(right) && rox::core::IsFinite(bottom) && rox::core::IsFinite(top) && left < right && bottom < top, "Perspective lateral bounds must be finite and ordered.");
		ASSERT(rox::core::IsFinite(nearPlane) && nearPlane > 0, "Perspective near plane must be finite and positive.");
		ASSERT(depth == EProjectionDepth::Forward || depth == EProjectionDepth::Reversed, "Projection depth mode must be a listed value.");
		const T n = nearPlane;
		const T width = static_cast<T>(right) - left;
		const T height = static_cast<T>(top) - bottom;
		const T x = projectionCoefficient<T>(2 * n / width);
		const T y = projectionCoefficient<T>(2 * n / height);
		ASSERT(x > 0 && y > 0, "Perspective scale coefficients must remain positive.");
		const bool reversed = depth == EProjectionDepth::Reversed;
		return {x,
		        0,
		        0,
		        0,
		        0,
		        y,
		        0,
		        0,
		        projectionCoefficient<T>(-(static_cast<T>(right) + left) / width),
		        projectionCoefficient<T>(-(static_cast<T>(top) + bottom) / height),
		        reversed ? T(0.0) : T(1.0),
		        1,
		        0,
		        0,
		        reversed ? nearPlane : -nearPlane,
		        0};
	}

	template <MathScalar T>
	Matrix4x4<T> PerspectiveFovInfiniteLH(T verticalFovRadians, T aspectRatio, T nearPlane, EProjectionDepth depth) noexcept
	{
		ASSERT(rox::core::IsFinite(verticalFovRadians) && verticalFovRadians > 0 && verticalFovRadians < rox::core::PI<T> && rox::core::IsFinite(aspectRatio) && aspectRatio > 0, "Perspective field of view and aspect must be finite and valid.");
		ASSERT(rox::core::IsFinite(nearPlane) && nearPlane > 0, "Perspective near plane must be finite and positive.");
		ASSERT(depth == EProjectionDepth::Forward || depth == EProjectionDepth::Reversed, "Projection depth mode must be a listed value.");
		const T scale = 1 / rox::core::Tan(static_cast<T>(verticalFovRadians) * T(0.5));
		const T x = projectionCoefficient<T>(scale / aspectRatio);
		const T y = projectionCoefficient<T>(scale);
		ASSERT(x > 0 && y > 0, "Perspective scale coefficients must remain positive.");
		const bool reversed = depth == EProjectionDepth::Reversed;
		return {x, 0, 0, 0, 0, y, 0, 0, 0, 0, reversed ? T(0.0) : T(1.0), 1, 0, 0, reversed ? nearPlane : -nearPlane, 0};
	}

	template <MathScalar T>
	Matrix4x4<T> ReverseProjectionDepth(const Matrix4x4<T>& projection) noexcept
	{
		ASSERT(projection.IsFinite(), "Projection inputs must be finite.");
		Matrix4x4<T> result = projection;

		for (uint32 row = 0; row < 4; ++row)
		{
			result[row][2] = projectionCoefficient<T>(static_cast<T>(projection[row][3]) - projection[row][2]);
		}

		return result;
	}

	template Matrix4x4<float32> PerspectiveOffCenterLH(float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane, EProjectionDepth depth) noexcept;
	template Matrix4x4<float64> PerspectiveOffCenterLH(float64 left, float64 right, float64 bottom, float64 top, float64 nearPlane, float64 farPlane, EProjectionDepth depth) noexcept;
	template Matrix4x4<float32> PerspectiveOffCenterInfiniteLH(float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, EProjectionDepth depth) noexcept;
	template Matrix4x4<float64> PerspectiveOffCenterInfiniteLH(float64 left, float64 right, float64 bottom, float64 top, float64 nearPlane, EProjectionDepth depth) noexcept;
	template Matrix4x4<float32> PerspectiveFovInfiniteLH(float32 verticalFovRadians, float32 aspectRatio, float32 nearPlane, EProjectionDepth depth) noexcept;
	template Matrix4x4<float64> PerspectiveFovInfiniteLH(float64 verticalFovRadians, float64 aspectRatio, float64 nearPlane, EProjectionDepth depth) noexcept;
	template Matrix4x4<float32> ReverseProjectionDepth(const Matrix4x4<float32>& projection) noexcept;
	template Matrix4x4<float64> ReverseProjectionDepth(const Matrix4x4<float64>& projection) noexcept;

	template <MathScalar T>
	bool ProjectionViewport<T>::IsValid() const noexcept
	{
		return rox::core::IsFinite(X) && rox::core::IsFinite(Y) && rox::core::IsFinite(Width) &&
		       rox::core::IsFinite(Height) && rox::core::IsFinite(MinDepth) && rox::core::IsFinite(MaxDepth) &&
		       Width > 0 && Height > 0 && MinDepth < MaxDepth;
	}

	template <MathScalar T>
	bool ProjectToScreen(const Vector3<T>& position, const Matrix4x4<T>& sourceToClip, const ProjectionViewport<T>& viewport, Vector3<T>* outScreen) noexcept
	{
		ASSERT(outScreen != nullptr, "ProjectToScreen requires a valid output pointer.");
		ASSERT(viewport.IsValid(), "Projection viewport must be valid.");
		ASSERT(position.IsFinite() && sourceToClip.IsFinite(), "Projection inputs must be finite.");
		detail::ProjectionVector3<T> ndc;

		if (!detail::ProjectionDivide<T>(detail::ProjectionTransform<T>(sourceToClip, {position.X, position.Y, position.Z}), ndc))
		{
			return false;
		}

		return detail::ProjectionNarrow<T>(
		    {viewport.X + (ndc.X + 1) * viewport.Width * T(0.5), viewport.Y + (1 - ndc.Y) * viewport.Height * T(0.5),
		     viewport.MinDepth + ndc.Z * (static_cast<T>(viewport.MaxDepth) - viewport.MinDepth)},
		    (*outScreen));
	}

	template <MathScalar T>
	bool UnprojectFromScreen(const Vector3<T>& screen, const Matrix4x4<T>& clipToSource, const ProjectionViewport<T>& viewport, Vector3<T>* outPosition) noexcept
	{
		ASSERT(outPosition != nullptr, "UnprojectFromScreen requires a valid output pointer.");
		ASSERT(viewport.IsValid(), "Projection viewport must be valid.");
		ASSERT(screen.IsFinite() && clipToSource.IsFinite(), "Projection inputs must be finite.");
		const detail::ProjectionVector3<T> ndc{(static_cast<T>(screen.X) - viewport.X) * 2 / viewport.Width - 1,
		                               1 - (static_cast<T>(screen.Y) - viewport.Y) * 2 / viewport.Height,
		                               (static_cast<T>(screen.Z) - viewport.MinDepth) /
		                               (static_cast<T>(viewport.MaxDepth) - viewport.MinDepth)};
		detail::ProjectionVector3<T> position;

		if (!detail::ProjectionDivide<T>(detail::ProjectionTransform<T>(clipToSource, ndc), position))
		{
			return false;
		}

		return detail::ProjectionNarrow<T>(position, (*outPosition));
	}

	template bool ProjectToScreen(const Vector3<float32>& position, const Matrix4x4<float32>& sourceToClip, const ProjectionViewport<float32>& viewport, Vector3<float32>* outScreen) noexcept;
	template bool ProjectToScreen(const Vector3<float64>& position, const Matrix4x4<float64>& sourceToClip, const ProjectionViewport<float64>& viewport, Vector3<float64>* outScreen) noexcept;
	template bool UnprojectFromScreen(const Vector3<float32>& screen, const Matrix4x4<float32>& clipToSource, const ProjectionViewport<float32>& viewport, Vector3<float32>* outPosition) noexcept;
	template bool UnprojectFromScreen(const Vector3<float64>& screen, const Matrix4x4<float64>& clipToSource, const ProjectionViewport<float64>& viewport, Vector3<float64>* outPosition) noexcept;

	template struct ProjectionViewport<float32>;
	template struct ProjectionViewport<float64>;

	template <MathScalar T>
	bool BuildScreenRay(const Vector2<T>& screen, const Matrix4x4<T>& clipToWorld, const ProjectionViewport<T>& viewport, Ray<T>* outRay, EProjectionDepth depth) noexcept
	{
		ASSERT(outRay != nullptr, "BuildScreenRay requires a valid output pointer.");
		ASSERT(viewport.IsValid(), "Projection viewport must be valid.");
		ASSERT(screen.IsFinite() && clipToWorld.IsFinite(), "Projection inputs must be finite.");
		ASSERT(depth == EProjectionDepth::Forward || depth == EProjectionDepth::Reversed, "Projection depth mode must be a listed value.");
		const T x = (static_cast<T>(screen.X) - viewport.X) * 2 / viewport.Width - 1;
		const T y = 1 - (static_cast<T>(screen.Y) - viewport.Y) * 2 / viewport.Height;
		detail::ProjectionVector3<T> nearPoint;

		if (!detail::ProjectionDivide<T>(detail::ProjectionTransform<T>(clipToWorld, {x, y, depth == EProjectionDepth::Forward ? T(0.0) : T(1.0)}), nearPoint))
		{
			return false;
		}

		Vector3<T> origin;

		if (!detail::ProjectionNarrow<T>(nearPoint, origin))
		{
			return false;
		}

		// An interior depth avoids the projective horizon at an infinite far plane.
		const detail::ProjectionVector4<T> middle = detail::ProjectionTransform<T>(clipToWorld, {x, y, T(0.5)});
		if (middle.W == T(0) || !rox::core::IsFinite(middle.X) || !rox::core::IsFinite(middle.Y) || !rox::core::IsFinite(middle.Z) || !rox::core::IsFinite(middle.W))
		{
			return false;
		}

		// Form the direction in homogeneous coordinates; the interior point need not fit in T after division.
		const T middleScale = rox::core::Max({rox::core::Abs(middle.X), rox::core::Abs(middle.Y), rox::core::Abs(middle.Z), rox::core::Abs(middle.W)});
		const T originScale = rox::core::Max({T(1), rox::core::Abs(origin.X), rox::core::Abs(origin.Y), rox::core::Abs(origin.Z)});
		Vector3<T> delta = Vector3<T>{middle.X / middleScale, middle.Y / middleScale, middle.Z / middleScale} / originScale - (origin / originScale) * (middle.W / middleScale);
		if (middle.W < T(0))
		{
			delta = -delta;
		}

		Vector3<T> direction;
		if (!delta.TryNormalize(&direction, T(0)))
		{
			return false;
		}

		*outRay = {origin, direction};
		return true;
	}

	template bool BuildScreenRay(const Vector2<float32>& screen, const Matrix4x4<float32>& clipToWorld, const ProjectionViewport<float32>& viewport, Ray<float32>* outRay, EProjectionDepth depth) noexcept;
	template bool BuildScreenRay(const Vector2<float64>& screen, const Matrix4x4<float64>& clipToWorld, const ProjectionViewport<float64>& viewport, Ray<float64>* outRay, EProjectionDepth depth) noexcept;
} // namespace rox::math
