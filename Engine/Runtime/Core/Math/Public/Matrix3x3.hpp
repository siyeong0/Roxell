#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Vector3.hpp>

#include <cstddef>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T>
	struct Matrix3x3 final
	{
		struct Storage
		{
			T Values[9];

			[[nodiscard]] constexpr const T* operator[](uint32 row) const noexcept
			{
				ASSERT(row < 3, "Matrix3x3 row index is out of range.");
				return Values + row * 3;
			}

			[[nodiscard]] constexpr T* operator[](uint32 row) noexcept
			{
				ASSERT(row < 3, "Matrix3x3 row index is out of range.");
				return Values + row * 3;
			}
		};

		Storage M;

		constexpr Matrix3x3() noexcept : M{{T(1.0), T(0.0), T(0.0), T(0.0), T(1.0), T(0.0), T(0.0), T(0.0), T(1.0)}}
		{
		}

		constexpr Matrix3x3(T m00, T m01, T m02, T m10, T m11, T m12, T m20, T m21, T m22) noexcept
		    : M{{m00, m01, m02, m10, m11, m12, m20, m21, m22}}
		{
		}

		[[nodiscard]] static constexpr Matrix3x3<T> Identity() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr Matrix3x3<T> Zero() noexcept
		{
			return {T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0), T(0.0)};
		}

		[[nodiscard]] static constexpr Matrix3x3<T> FromRows(const Vector3<T>& row0, const Vector3<T>& row1, const Vector3<T>& row2) noexcept
		{
			return {row0.X, row0.Y, row0.Z, row1.X, row1.Y, row1.Z, row2.X, row2.Y, row2.Z};
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

		[[nodiscard]] Vector3<T> GetRow(uint32 row) const noexcept
		{
			ASSERT(row < 3, "Matrix3x3 row index is out of range.");
			return {M[row][0], M[row][1], M[row][2]};
		}

		[[nodiscard]] Vector3<T> GetColumn(uint32 column) const noexcept
		{
			ASSERT(column < 3, "Matrix3x3 column index is out of range.");
			return {M[0][column], M[1][column], M[2][column]};
		}

		[[nodiscard]] bool IsFinite() const noexcept
		{
			return GetRow(0).IsFinite() && GetRow(1).IsFinite() && GetRow(2).IsFinite();
		}

		[[nodiscard]] Vector3<T> TransformVector(const Vector3<T>& vector) const noexcept;
		[[nodiscard]] Matrix3x3<T> Transposed() const noexcept;
		[[nodiscard]] T Determinant() const noexcept;

		// Epsilon bounds the determinant after scaling by the largest entry. Failure preserves output.
		[[nodiscard]] bool TryInverse(Matrix3x3<T>* outInverse, T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] Matrix3x3<T> Inversed(T epsilon = T(1.0e-8)) const noexcept;

		[[nodiscard]] static Matrix3x3<T> RotationX(T radians) noexcept;
		[[nodiscard]] static Matrix3x3<T> RotationY(T radians) noexcept;
		[[nodiscard]] static Matrix3x3<T> RotationZ(T radians) noexcept;
		[[nodiscard]] static Matrix3x3<T> RotationAxis(const Vector3<T>& axis, T radians) noexcept;

		[[nodiscard]] Matrix3x3<T> operator*(const Matrix3x3<T>& other) const noexcept;

		template <MathScalar U>
		explicit constexpr Matrix3x3(const Matrix3x3<U>& other) noexcept
		{
			for (uint32 index = 0; index < 9; ++index)
			{
				M.Values[index] = static_cast<T>(other.M.Values[index]);
			}
		}
	};

	using Matrix3x3f = Matrix3x3<float32>;
	using Matrix3x3d = Matrix3x3<float64>;

	static_assert(sizeof(Matrix3x3<float32>) == sizeof(float32) * 9);
	static_assert(sizeof(Matrix3x3<float64>) == sizeof(float64) * 9);
	static_assert(sizeof(Matrix3x3<float32>::Storage) == sizeof(float32) * 9);
	static_assert(sizeof(Matrix3x3<float64>::Storage) == sizeof(float64) * 9);
	static_assert(offsetof(Matrix3x3<float32>, M) == 0);
	static_assert(offsetof(Matrix3x3<float64>, M) == 0);
	static_assert(core::IsStandardLayout<Matrix3x3<float32>> && core::IsTriviallyCopyable<Matrix3x3<float32>>);
	static_assert(core::IsStandardLayout<Matrix3x3<float64>> && core::IsTriviallyCopyable<Matrix3x3<float64>>);
} // namespace rox::math
