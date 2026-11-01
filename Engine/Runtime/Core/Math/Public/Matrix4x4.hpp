#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <Matrix3x3.hpp>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Vector3.hpp>
#include <Vector4.hpp>

#include <cstddef>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T>
	struct Quaternion;

	template <MathScalar T>
	struct Matrix4x4 final
	{
		struct Storage
		{
			T Values[16];

			[[nodiscard]] constexpr const T* operator[](uint32 row) const noexcept
			{
				ASSERT(row < 4, "Matrix4x4 row index is out of range.");
				return Values + row * 4;
			}

			[[nodiscard]] constexpr T* operator[](uint32 row) noexcept
			{
				ASSERT(row < 4, "Matrix4x4 row index is out of range.");
				return Values + row * 4;
			}
		};

		Storage M;

		constexpr Matrix4x4() noexcept
		    : M{{T(1.0), T(0.0), T(0.0), T(0.0), T(0.0), T(1.0), T(0.0), T(0.0), T(0.0), T(0.0), T(1.0), T(0.0), T(0.0), T(0.0), T(0.0), T(1.0)}}
		{
		}

		constexpr Matrix4x4(T m00, T m01, T m02, T m03, T m10, T m11, T m12, T m13, T m20, T m21, T m22, T m23, T m30, T m31, T m32, T m33) noexcept
		    : M{{m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33}}
		{
		}

		[[nodiscard]] static constexpr Matrix4x4<T> Identity() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr Matrix4x4<T> Zero() noexcept
		{
			return {T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0)};
		}

		[[nodiscard]] constexpr const T* operator[](uint32 row) const noexcept
		{
			return M[row];
		}

		[[nodiscard]] constexpr T* operator[](uint32 row) noexcept
		{
			return M[row];
		}

		[[nodiscard]] constexpr const T* Data() const noexcept
		{
			return M.Values;
		}

		[[nodiscard]] constexpr T* Data() noexcept
		{
			return M.Values;
		}

		[[nodiscard]] Vector4<T> GetRow(uint32 row) const noexcept
		{
			ASSERT(row < 4, "Matrix4x4 row index is out of range.");
			return {M[row][0], M[row][1], M[row][2], M[row][3]};
		}

		[[nodiscard]] Vector4<T> GetColumn(uint32 column) const noexcept
		{
			ASSERT(column < 4, "Matrix4x4 column index is out of range.");
			return {M[0][column], M[1][column], M[2][column], M[3][column]};
		}

		[[nodiscard]] bool IsFinite() const noexcept
		{
			return GetRow(0).IsFinite() && GetRow(1).IsFinite() && GetRow(2).IsFinite() && GetRow(3).IsFinite();
		}

		[[nodiscard]] bool IsAffine() const noexcept
		{
			return IsFinite() && M[0][3] == T(0.0) && M[1][3] == T(0.0) && M[2][3] == T(0.0) && M[3][3] == T(1.0);
		}

		[[nodiscard]] Matrix3x3<T> ExtractLinearMatrix() const noexcept
		{
			return {M[0][0], M[0][1], M[0][2], M[1][0], M[1][1], M[1][2], M[2][0], M[2][1], M[2][2]};
		}

		[[nodiscard]] T Determinant() const noexcept;

		[[nodiscard]] static Matrix4x4<T> Translation(const Vector3<T>& translation) noexcept;
		[[nodiscard]] static Matrix4x4<T> Scale(const Vector3<T>& scale) noexcept;
		[[nodiscard]] static Matrix4x4<T> RotationX(T radians) noexcept;
		[[nodiscard]] static Matrix4x4<T> RotationY(T radians) noexcept;
		[[nodiscard]] static Matrix4x4<T> RotationZ(T radians) noexcept;
		[[nodiscard]] static Matrix4x4<T> RotationAxis(const Vector3<T>& axis, T radians) noexcept;
		[[nodiscard]] static Matrix4x4<T> FromTRS(const Vector3<T>& translation, const Quaternion<T>& rotation, const Vector3<T>& scale) noexcept;
		[[nodiscard]] static Matrix4x4<T> LookAtLH(const Vector3<T>& eye, const Vector3<T>& target, const Vector3<T>& up) noexcept;
		[[nodiscard]] static Matrix4x4<T> PerspectiveFovLH(T verticalFovRadians, T aspectRatio, T nearPlane, T farPlane) noexcept;
		[[nodiscard]] static Matrix4x4<T> OrthographicOffCenterLH(T left, T right, T bottom, T top, T nearPlane, T farPlane) noexcept;

		[[nodiscard]] Vector4<T> TransformVector4(const Vector4<T>& vector) const noexcept;
		[[nodiscard]] Vector3<T> TransformPosition(const Vector3<T>& position, T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] Vector3<T> TransformDirection(const Vector3<T>& direction) const noexcept;

		[[nodiscard]] Vector3<T> ExtractTranslation() const noexcept
		{
			return {M[3][0], M[3][1], M[3][2]};
		}

		[[nodiscard]] Matrix4x4<T> Transposed() const noexcept;

		// Epsilon bounds elimination pivots after scaling by the largest entry. Failure preserves output.
		[[nodiscard]] bool TryInverse(Matrix4x4<T>* outInverse, T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] Matrix4x4<T> Inversed(T epsilon = T(1.0e-8)) const noexcept;

		[[nodiscard]] Matrix4x4<T> operator*(const Matrix4x4<T>& other) const noexcept;

		template <MathScalar U>
		explicit constexpr Matrix4x4(const Matrix4x4<U>& other) noexcept
		{
			for (uint32 index = 0; index < 16; ++index)
			{
				M.Values[index] = static_cast<T>(other.M.Values[index]);
			}
		}
	};

	using Matrix4x4f = Matrix4x4<float32>;
	using Matrix4x4d = Matrix4x4<float64>;

	static_assert(sizeof(Matrix4x4<float32>) == sizeof(float32) * 16);
	static_assert(sizeof(Matrix4x4<float64>) == sizeof(float64) * 16);
	static_assert(sizeof(Matrix4x4<float32>::Storage) == sizeof(float32) * 16);
	static_assert(sizeof(Matrix4x4<float64>::Storage) == sizeof(float64) * 16);
	static_assert(offsetof(Matrix4x4<float32>, M) == 0);
	static_assert(offsetof(Matrix4x4<float64>, M) == 0);
	static_assert(core::IsStandardLayout<Matrix4x4<float32>> && core::IsTriviallyCopyable<Matrix4x4<float32>>);
	static_assert(core::IsStandardLayout<Matrix4x4<float64>> && core::IsTriviallyCopyable<Matrix4x4<float64>>);
} // namespace rox::math
