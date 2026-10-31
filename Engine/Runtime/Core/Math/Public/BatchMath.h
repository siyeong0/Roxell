#pragma once

#include <MathScalar.hpp>

#include <AffineTransform.h>
#include <Types.h>

#include <cstddef>

namespace rox::math
{
	// Ranges may be disjoint or exactly in-place. Empty ranges may be null; partial overlap is invalid.
	// Inputs must be finite and results representable as the finite scalar range. No extra alignment is required.
	template <MathScalar T = float32>
	void TransformPositions(const AffineTransform<T>& transform, const Vector3<T>* source, Vector3<T>* destination, rox::core::usize count) noexcept;

	template <MathScalar T = float32>
	void TransformDirections(const AffineTransform<T>& transform, const Vector3<T>* source, Vector3<T>* destination, rox::core::usize count) noexcept;

	// Uses the representable inverse-transpose normal matrix once per batch; does not normalize results.
	template <MathScalar T = float32>
	void TransformNormals(const AffineTransform<T>& transform, const Vector3<T>* source, Vector3<T>* outNormals, rox::core::usize count) noexcept;

	// Homogeneous row-vector multiplication without perspective division.
	template <MathScalar T = float32>
	void TransformVectors(const Matrix4x4<T>& transform, const Vector4<T>* source, Vector4<T>* outVectors, rox::core::usize count) noexcept;

	// Output floats must not overlap either input range. The two input ranges may overlap each other.
	template <MathScalar T = float32>
	void DotProducts(const Vector3<T>* left, const Vector3<T>* right, T* outProducts, rox::core::usize count) noexcept;
} // namespace rox::math
