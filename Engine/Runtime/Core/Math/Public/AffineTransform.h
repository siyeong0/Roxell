#pragma once

#include <MathScalar.hpp>

#include <Matrix3x3.hpp>
#include <Matrix4x4.hpp>
#include <TypeTraits.hpp>
#include <Vector3.hpp>

namespace rox::math
{
	// Row vectors: p maps to p*Linear+Translation; composition retains shear.
	template <MathScalar T = float32>
	struct AffineTransform final
	{
		Matrix3x3<T> Linear = Matrix3x3<T>::Identity();
		Vector3<T> Translation = Vector3<T>::Zero();
		constexpr AffineTransform() noexcept = default;

		constexpr AffineTransform(const Matrix3x3<T>& linear, const Vector3<T>& translation) noexcept
		    : Linear(linear), Translation(translation)
		{
		}

		[[nodiscard]] static constexpr AffineTransform<T> Identity() noexcept
		{
			return {};
		}

		[[nodiscard]] static AffineTransform<T> FromMatrix(const Matrix4x4<T>& matrix) noexcept;
		[[nodiscard]] Matrix4x4<T> ToMatrix() const noexcept;
		[[nodiscard]] bool IsFinite() const noexcept;
		[[nodiscard]] Vector3<T> TransformPosition(const Vector3<T>& position) const noexcept;
		[[nodiscard]] Vector3<T> TransformDirection(const Vector3<T>& direction) const noexcept;

		// Inverse operations require finite inputs and representable inverse/output coefficients.
		// No epsilon cutoff is applied. Unsafe cofactor products are evaluated with separate binary exponents.
		[[nodiscard]] AffineTransform<T> Inversed() const noexcept;
		[[nodiscard]] Vector3<T> InverseTransformPosition(const Vector3<T>& position) const noexcept;
		[[nodiscard]] Vector3<T> InverseTransformDirection(const Vector3<T>& direction) const noexcept;
		[[nodiscard]] Matrix3x3<T> NormalMatrix() const noexcept;
		[[nodiscard]] Vector3<T> TransformNormal(const Vector3<T>& normal) const noexcept;
		[[nodiscard]] AffineTransform<T> RelativeTo(const AffineTransform<T>& parentWorld) const noexcept;

		// This transform is applied first; next is applied second.
		[[nodiscard]] AffineTransform<T> operator*(const AffineTransform<T>& next) const noexcept;

		template <MathScalar U>
		explicit constexpr AffineTransform(const AffineTransform<U>& other) noexcept
		    : Linear(other.Linear), Translation(other.Translation)
		{
		}
	};

	static_assert(sizeof(AffineTransform<float32>) == 12 * sizeof(float32));
	static_assert(sizeof(AffineTransform<float64>) == 12 * sizeof(float64));
	static_assert(alignof(AffineTransform<float32>) == alignof(float32));
	static_assert(alignof(AffineTransform<float64>) == alignof(float64));
	static_assert(offsetof(AffineTransform<float32>, Translation) == 9 * sizeof(float32));
	static_assert(offsetof(AffineTransform<float64>, Translation) == 9 * sizeof(float64));
	static_assert(core::IsStandardLayout<AffineTransform<float32>> && core::IsTriviallyCopyable<AffineTransform<float32>>);
	static_assert(core::IsStandardLayout<AffineTransform<float64>> && core::IsTriviallyCopyable<AffineTransform<float64>>);
} // namespace rox::math
