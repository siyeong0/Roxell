#pragma once

#include <MathScalar.hpp>

#include <AffineTransform.h>
#include <Box.h>
#include <NumericLimits.hpp>
#include <Plane.h>
#include <Ray.h>
#include <Sphere.h>
#include <Triangle.h>
#include <Vocabulary.hpp>

namespace rox::math
{
	// Conservative affine bounds, including the scalar evaluation of each corner. Zero scale is valid.
	// Invalid/empty input or nonrepresentable intermediates/output return an empty optional.
	template <MathScalar T = float32>
	[[nodiscard]] core::Optional<Box<T>> TransformedBounds(const Box<T>& box, const AffineTransform<T>& transform) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Intersect(const Ray<T>& ray, const Triangle<T>& triangle, T* outParameter, T maxParameter = rox::core::NumericLimits<T>::max()) noexcept;

	template <MathScalar T = float32>
	struct RayInterval final
	{
		T Enter = 0;
		T Exit = 0;
	};

	template <MathScalar T = float32>
	[[nodiscard]] bool Intersect(const Ray<T>& ray, const Box<T>& box, RayInterval<T>* outInterval, T maxParameter = rox::core::NumericLimits<T>::max()) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] bool Intersect(const Ray<T>& ray, const Sphere<T>& sphere, RayInterval<T>* outInterval, T maxParameter = rox::core::NumericLimits<T>::max()) noexcept;

	// Finite coefficients and a nonzero normal are required. False preserves the output,
	// including when the parameter remains nonfinite after rescaling.
	template <MathScalar T = float32>
	[[nodiscard]] bool Intersect(const Ray<T>& ray, const Plane<T>& plane, T* outParameter, T maxParameter = rox::core::NumericLimits<T>::max()) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> ClosestPoint(const Vector3<T>& point, const Plane<T>& plane) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> ClosestPoint(const Vector3<T>& point, const Box<T>& box) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> ClosestPoint(const Vector3<T>& point, const Sphere<T>& sphere) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T DistanceSquared(const Vector3<T>& point, const Plane<T>& plane) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T DistanceSquared(const Vector3<T>& point, const Box<T>& box) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T DistanceSquared(const Vector3<T>& point, const Sphere<T>& sphere) noexcept;
} // namespace rox::math
