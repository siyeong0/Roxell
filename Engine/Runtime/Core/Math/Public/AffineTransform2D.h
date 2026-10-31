#pragma once

#include <MathScalar.hpp>

#include <Matrix3x3.hpp>
#include <TypeTraits.hpp>
#include <Vector2.hpp>

namespace rox::math
{
	// Row vectors: p maps to p.X*AxisX+p.Y*AxisY+Translation.
	// Operations require finite inputs and outputs representable in the finite scalar range.
	template <MathScalar T = float32>
	struct AffineTransform2D final
	{
		Vector2<T> AxisX = {1, 0};
		Vector2<T> AxisY = {0, 1};
		Vector2<T> Translation = {0, 0};

		constexpr AffineTransform2D() noexcept = default;

		constexpr AffineTransform2D(const Vector2<T>& axisX, const Vector2<T>& axisY, const Vector2<T>& translation) noexcept
		    : AxisX(axisX), AxisY(axisY), Translation(translation)
		{
		}

		[[nodiscard]] static constexpr AffineTransform2D<T> Identity() noexcept
		{
			return {};
		}

		[[nodiscard]] bool IsFinite() const noexcept;

		// Finite inputs are required; zero and negative scale are valid.
		[[nodiscard]] static AffineTransform2D<T> FromTRS(const Vector2<T>& translation, T radians, const Vector2<T>& scale) noexcept;

		// Requires finite matrix lanes and the exact final column (0,0,1).
		[[nodiscard]] static AffineTransform2D<T> FromMatrix(const Matrix3x3<T>& matrix) noexcept;
		[[nodiscard]] Matrix3x3<T> ToMatrix() const noexcept;
		[[nodiscard]] Vector2<T> TransformPosition(const Vector2<T>& position) const noexcept;
		[[nodiscard]] Vector2<T> TransformDirection(const Vector2<T>& direction) const noexcept;

		// This transform is applied first; next is applied second. Shear is retained.
		[[nodiscard]] AffineTransform2D<T> operator*(const AffineTransform2D<T>& next) const noexcept;

		// Inverse operations require representable inverse/output coefficients; no epsilon cutoff is applied.
		// Unsafe cofactor determinants are recomputed with row scaling.
		[[nodiscard]] AffineTransform2D<T> Inversed() const noexcept;
		[[nodiscard]] Vector2<T> InverseTransformPosition(const Vector2<T>& position) const noexcept;
		[[nodiscard]] Vector2<T> InverseTransformDirection(const Vector2<T>& direction) const noexcept;

		// Uses the inverse transpose without normalization, including for a zero normal.
		[[nodiscard]] Vector2<T> TransformNormal(const Vector2<T>& normal) const noexcept;
		[[nodiscard]] AffineTransform2D<T> RelativeTo(const AffineTransform2D<T>& parentWorld) const noexcept;

		template <MathScalar U>
		explicit constexpr AffineTransform2D(const AffineTransform2D<U>& other) noexcept
		    : AxisX(other.AxisX), AxisY(other.AxisY), Translation(other.Translation)
		{
		}
	};

	static_assert(sizeof(AffineTransform2D<float32>) == 6 * sizeof(float32));
	static_assert(sizeof(AffineTransform2D<float64>) == 6 * sizeof(float64));
	static_assert(alignof(AffineTransform2D<float32>) == alignof(float32));
	static_assert(alignof(AffineTransform2D<float64>) == alignof(float64));
	static_assert(offsetof(AffineTransform2D<float32>, AxisX) == 0 && offsetof(AffineTransform2D<float32>, AxisY) == 2 * sizeof(float32));
	static_assert(offsetof(AffineTransform2D<float64>, AxisX) == 0 && offsetof(AffineTransform2D<float64>, AxisY) == 2 * sizeof(float64));
	static_assert(offsetof(AffineTransform2D<float32>, Translation) == 4 * sizeof(float32));
	static_assert(offsetof(AffineTransform2D<float64>, Translation) == 4 * sizeof(float64));
	static_assert(rox::core::IsStandardLayout<AffineTransform2D<float32>> && rox::core::IsTriviallyCopyable<AffineTransform2D<float32>>);
	static_assert(rox::core::IsStandardLayout<AffineTransform2D<float64>> && rox::core::IsTriviallyCopyable<AffineTransform2D<float64>>);
} // namespace rox::math
