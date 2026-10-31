#pragma once

#include <MathScalar.hpp>

#include <Box.h>
#include <Matrix4x4.hpp>
#include <Plane.h>
#include <Sphere.h>
#include <TypeTraits.hpp>
#include <Types.h>

#include <type_traits>

namespace rox::math
{
	enum class EFrustumPlane : uint8
	{
		Left,
		Right,
		Bottom,
		Top,
		Near,
		Far,
		Count
	};

	enum class EFrustumContainment : uint8
	{
		Outside,
		Intersecting,
		Inside
	};

	template <MathScalar T = float32>
	struct Frustum final
	{
		Plane<T> Left;
		Plane<T> Right;
		Plane<T> Bottom;
		Plane<T> Top;
		Plane<T> Near;
		Plane<T> Far;

		// Infinite depth boundaries use a zero normal and positive distance; side planes remain finite.
		[[nodiscard]] static Frustum<T> FromViewProjection(const Matrix4x4<T>& viewProjection) noexcept;
		[[nodiscard]] const Plane<T>& GetPlane(EFrustumPlane plane) const noexcept;
		[[nodiscard]] Plane<T>& GetPlane(EFrustumPlane plane) noexcept;
		[[nodiscard]] bool IsValid(T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] bool ContainsPoint(const Vector3<T>& point) const noexcept;
		[[nodiscard]] bool Intersects(const Sphere<T>& sphere) const noexcept;
		[[nodiscard]] bool Intersects(const Box<T>& box) const noexcept;
		[[nodiscard]] EFrustumContainment Classify(const Box<T>& box) const noexcept;
	};

	static_assert(sizeof(Frustum<float32>) == sizeof(float32) * 24);
	static_assert(sizeof(Frustum<float64>) == sizeof(float64) * 24);
	static_assert(rox::core::IsStandardLayout<Frustum<float32>> && rox::core::IsTriviallyCopyable<Frustum<float32>>);
	static_assert(rox::core::IsStandardLayout<Frustum<float64>> && rox::core::IsTriviallyCopyable<Frustum<float64>>);
} // namespace rox::math
