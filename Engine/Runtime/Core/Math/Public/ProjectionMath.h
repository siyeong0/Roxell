#pragma once

#include <MathScalar.hpp>
#include <Matrix4x4.hpp>
#include <Ray.h>
#include <TypeTraits.hpp>
#include <Vector2.hpp>
#include <Vector3.hpp>

#include <type_traits>

namespace rox::math
{
	template <MathScalar T = float32>
	struct ProjectionViewport final
	{
		T X = T(0.0);
		T Y = T(0.0);
		T Width = T(1.0);
		T Height = T(1.0);

		T MinDepth = T(0.0);
		T MaxDepth = T(1.0);
		[[nodiscard]] bool IsValid() const noexcept;
	};

	static_assert(sizeof(ProjectionViewport<float32>) == 6 * sizeof(float32));
	static_assert(sizeof(ProjectionViewport<float64>) == 6 * sizeof(float64));
	static_assert(core::IsStandardLayout<ProjectionViewport<float32>> && core::IsTriviallyCopyable<ProjectionViewport<float32>>);
	static_assert(core::IsStandardLayout<ProjectionViewport<float64>> && core::IsTriviallyCopyable<ProjectionViewport<float64>>);
	enum class EProjectionDepth : uint8
	{
		Forward,
		Reversed
	};

	// Bounds lie on the physical positive near plane; reversed depth preserves these lateral bounds.
	template <MathScalar T = float32>
	[[nodiscard]] Matrix4x4<T> PerspectiveOffCenterLH(T left, T right, T bottom, T top, T nearPlane, T farPlane, EProjectionDepth depth = EProjectionDepth::Forward) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Matrix4x4<T> PerspectiveOffCenterInfiniteLH(
	    T left, T right, T bottom, T top, T nearPlane, EProjectionDepth depth = EProjectionDepth::Forward) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] Matrix4x4<T> PerspectiveFovInfiniteLH(T verticalFovRadians, T aspectRatio, T nearPlane, EProjectionDepth depth = EProjectionDepth::Forward) noexcept;

	// Replaces normalized depth z by 1-z while preserving homogeneous x, y and w.
	template <MathScalar T = float32>
	[[nodiscard]] Matrix4x4<T> ReverseProjectionDepth(const Matrix4x4<T>& projection) noexcept;

	// Continuous top-left pixel coordinates; no clipping or half-pixel offset. A horizon/range failure preserves
	// output.
	template <MathScalar T = float32>
	[[nodiscard]] bool ProjectToScreen(const Vector3<T>& position, const Matrix4x4<T>& sourceToClip, const ProjectionViewport<T>& viewport, Vector3<T>* outScreen) noexcept;

	// The caller supplies the inverse transform. Screen/depth values are not clamped; failure preserves output.
	template <MathScalar T = float32>
	[[nodiscard]] bool UnprojectFromScreen(const Vector3<T>& screen, const Matrix4x4<T>& clipToSource, const ProjectionViewport<T>& viewport, Vector3<T>* outPosition) noexcept;

	// The ray starts on the physical near plane and has unit direction. Infinite far projections are supported.
	// Inputs use continuous pixels; a horizon, range or degenerate-direction failure preserves the complete output.
	template <MathScalar T = float32>
	[[nodiscard]] bool BuildScreenRay(const Vector2<T>& screen, const Matrix4x4<T>& clipToWorld, const ProjectionViewport<T>& viewport, Ray<T>* outRay, EProjectionDepth depth = EProjectionDepth::Forward) noexcept;
} // namespace rox::math
