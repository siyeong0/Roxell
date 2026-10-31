#pragma once

#include <MathScalar.hpp>

#include <Vector2.hpp>
#include <Vector3.hpp>
#include <Vector4.hpp>

namespace rox::math
{
	// Controls are finite and t is in [0,1]. Tangents are unnormalized derivatives with respect to t.
	// Values and tangents must fit the finite scalar range; nonnegative length estimates may overflow to positive
	// infinity.
	template <MathScalar T = float32>
	[[nodiscard]] T QuadraticBezier(T p0, T p1, T p2, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> QuadraticBezier(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> QuadraticBezier(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector4<T> QuadraticBezier(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T QuadraticBezierTangent(T p0, T p1, T p2, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> QuadraticBezierTangent(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> QuadraticBezierTangent(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector4<T> QuadraticBezierTangent(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T CubicBezier(T p0, T p1, T p2, T p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> CubicBezier(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> CubicBezier(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector4<T> CubicBezier(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T CubicBezierTangent(T p0, T p1, T p2, T p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> CubicBezierTangent(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> CubicBezierTangent(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector4<T> CubicBezierTangent(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T Hermite(T p0, T tangent0, T p1, T tangent1, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> Hermite(const Vector2<T>& p0, const Vector2<T>& tangent0, const Vector2<T>& p1, const Vector2<T>& tangent1, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> Hermite(const Vector3<T>& p0, const Vector3<T>& tangent0, const Vector3<T>& p1, const Vector3<T>& tangent1, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector4<T> Hermite(const Vector4<T>& p0, const Vector4<T>& tangent0, const Vector4<T>& p1, const Vector4<T>& tangent1, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T HermiteTangent(T p0, T tangent0, T p1, T tangent1, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> HermiteTangent(const Vector2<T>& p0, const Vector2<T>& tangent0, const Vector2<T>& p1, const Vector2<T>& tangent1, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> HermiteTangent(const Vector3<T>& p0, const Vector3<T>& tangent0, const Vector3<T>& p1, const Vector3<T>& tangent1, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector4<T> HermiteTangent(const Vector4<T>& p0, const Vector4<T>& tangent0, const Vector4<T>& p1, const Vector4<T>& tangent1, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T CatmullRom(T p0, T p1, T p2, T p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> CatmullRom(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> CatmullRom(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector4<T> CatmullRom(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T CatmullRomTangent(T p0, T p1, T p2, T p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector2<T> CatmullRomTangent(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector3<T> CatmullRomTangent(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, T t) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Vector4<T> CatmullRomTangent(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, T t) noexcept;

	// Positive subdivisions select uniform chords over [0,1]; the estimate has no absolute error guarantee.
	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateQuadraticBezierLength(T p0, T p1, T p2, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateQuadraticBezierLength(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateQuadraticBezierLength(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateQuadraticBezierLength(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateCubicBezierLength(T p0, T p1, T p2, T p3, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateCubicBezierLength(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateCubicBezierLength(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateCubicBezierLength(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateHermiteLength(T p0, T tangent0, T p1, T tangent1, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateHermiteLength(const Vector2<T>& p0, const Vector2<T>& tangent0, const Vector2<T>& p1, const Vector2<T>& tangent1, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateHermiteLength(const Vector3<T>& p0, const Vector3<T>& tangent0, const Vector3<T>& p1, const Vector3<T>& tangent1, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateHermiteLength(const Vector4<T>& p0, const Vector4<T>& tangent0, const Vector4<T>& p1, const Vector4<T>& tangent1, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateCatmullRomLength(T p0, T p1, T p2, T p3, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateCatmullRomLength(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateCatmullRomLength(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, uint32 subdivisions = 32) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T ApproximateCatmullRomLength(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, uint32 subdivisions = 32) noexcept;
} // namespace rox::math
