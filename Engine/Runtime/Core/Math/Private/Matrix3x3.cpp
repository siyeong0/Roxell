#include <Matrix3x3.hpp>
#include <Numeric.hpp>
#include <Comparison.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rox::math
{
	template <MathScalar T>
	Vector3<T> Matrix3x3<T>::TransformVector(const Vector3<T>& vector) const noexcept
	{
		return {vector.X * M[0][0] + vector.Y * M[1][0] + vector.Z * M[2][0],
		        vector.X * M[0][1] + vector.Y * M[1][1] + vector.Z * M[2][1],
		        vector.X * M[0][2] + vector.Y * M[1][2] + vector.Z * M[2][2]};
	}

	template <MathScalar T>
	Matrix3x3<T> Matrix3x3<T>::Transposed() const noexcept
	{
		return {M[0][0], M[1][0], M[2][0],
		        M[0][1], M[1][1], M[2][1],
		        M[0][2], M[1][2], M[2][2]};
	}

	template <MathScalar T>
	T Matrix3x3<T>::Determinant() const noexcept
	{
		return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) -
		       M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
		       M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
	}

	template <MathScalar T>
	bool Matrix3x3<T>::TryInverse(Matrix3x3<T>* outInverse, T epsilon) const noexcept
	{
		ASSERT(outInverse != nullptr, "Inverse Matrix3x3 output must not be null.");
		ASSERT(rox::core::IsFinite(epsilon) && epsilon >= T(0.0), "Matrix3x3 inversion epsilon must be finite and non-negative.");

		T maximumMagnitude = T(0.0);

		for (uint32 index = 0; index < 9; ++index)
		{
			if (!rox::core::IsFinite(Data()[index]))
			{
				return false;
			}

			maximumMagnitude = rox::core::Max(maximumMagnitude, rox::core::Abs(Data()[index]));
		}

		if (maximumMagnitude == T(0.0))
		{
			return false;
		}

		// Scale the input before evaluating cofactors in the selected scalar type.
		const T inverseScale = T(1.0) / maximumMagnitude;
		const T normalized[3][3]{
		    {M[0][0] * inverseScale, M[0][1] * inverseScale, M[0][2] * inverseScale},
		    {M[1][0] * inverseScale, M[1][1] * inverseScale, M[1][2] * inverseScale},
		    {M[2][0] * inverseScale, M[2][1] * inverseScale, M[2][2] * inverseScale}
		};

		const T cofactors[3][3]{
		    {
		        normalized[1][1] * normalized[2][2] - normalized[1][2] * normalized[2][1],
		        normalized[0][2] * normalized[2][1] - normalized[0][1] * normalized[2][2],
		        normalized[0][1] * normalized[1][2] - normalized[0][2] * normalized[1][1]
		    },
		    {
		        normalized[1][2] * normalized[2][0] - normalized[1][0] * normalized[2][2],
		        normalized[0][0] * normalized[2][2] - normalized[0][2] * normalized[2][0],
		        normalized[0][2] * normalized[1][0] - normalized[0][0] * normalized[1][2]
		    },
		    {
		        normalized[1][0] * normalized[2][1] - normalized[1][1] * normalized[2][0],
		        normalized[0][1] * normalized[2][0] - normalized[0][0] * normalized[2][1],
		        normalized[0][0] * normalized[1][1] - normalized[0][1] * normalized[1][0]
		    }
		};

		const T determinant = normalized[0][0] * cofactors[0][0] + normalized[0][1] * cofactors[1][0] + normalized[0][2] * cofactors[2][0];

		if (!rox::core::IsFinite(determinant) || rox::core::Abs(determinant) <= epsilon)
		{
			return false;
		}

		Matrix3x3<T> inverse = Zero();

		for (uint32 row = 0; row < 3; ++row)
		{
			for (uint32 column = 0; column < 3; ++column)
			{
				const T value = cofactors[row][column] / determinant * inverseScale;

				if (!rox::core::IsFinite(value))
				{
					return false;
				}

				inverse.M[row][column] = static_cast<T>(value);
			}
		}

		*outInverse = inverse;
		return true;
	}

	template <MathScalar T>
	Matrix3x3<T> Matrix3x3<T>::Inversed(T epsilon) const noexcept
	{
		Matrix3x3<T> result;
		const bool bInverted = TryInverse(&result, epsilon);
		ASSERT(bInverted, "Matrix3x3 must be finite and invertible.");
		return result;
	}

	template <MathScalar T>
	Matrix3x3<T> Matrix3x3<T>::RotationX(T radians) noexcept
	{
		const T cosine = rox::core::Cos(radians);
		const T sine = rox::core::Sin(radians);

		return {T(1.0), T(0.0), T(0.0),
		        T(0.0), cosine, sine,
		        T(0.0), -sine,  cosine};
	}

	template <MathScalar T>
	Matrix3x3<T> Matrix3x3<T>::RotationY(T radians) noexcept
	{
		const T cosine = rox::core::Cos(radians);
		const T sine = rox::core::Sin(radians);

		return {cosine, T(0.0), -sine,
		        T(0.0), T(1.0), T(0.0),
		        sine,   T(0.0), cosine};
	}

	template <MathScalar T>
	Matrix3x3<T> Matrix3x3<T>::RotationZ(T radians) noexcept
	{
		const T cosine = rox::core::Cos(radians);
		const T sine = rox::core::Sin(radians);

		return {cosine, sine,   T(0.0),
		        -sine,  cosine, T(0.0),
		        T(0.0), T(0.0), T(1.0)};
	}

	template <MathScalar T>
	Matrix3x3<T> Matrix3x3<T>::RotationAxis(const Vector3<T>& axis, T radians) noexcept
	{
		const Vector3<T> normalizedAxis = axis.Normalized();
		const T x = normalizedAxis.X;
		const T y = normalizedAxis.Y;
		const T z = normalizedAxis.Z;
		const T cosine = rox::core::Cos(radians);
		const T sine = -rox::core::Sin(radians);
		const T oneMinusCosine = T(1.0) - cosine;

		return {cosine + x * x * oneMinusCosine,    x * y * oneMinusCosine - z * sine, x * z * oneMinusCosine + y * sine,
		        y * x * oneMinusCosine + z * sine, cosine + y * y * oneMinusCosine,    y * z * oneMinusCosine - x * sine,
		        z * x * oneMinusCosine - y * sine, z * y * oneMinusCosine + x * sine, cosine + z * z * oneMinusCosine};
	}

	template <MathScalar T>
	Matrix3x3<T> Matrix3x3<T>::operator*(const Matrix3x3<T>& other) const noexcept
	{
		Matrix3x3<T> result = Zero();

		for (uint32 row = 0; row < 3; ++row)
		{
			for (uint32 column = 0; column < 3; ++column)
			{
				for (uint32 index = 0; index < 3; ++index)
				{
					result.M[row][column] += M[row][index] * other.M[index][column];
				}
			}
		}

		return result;
	}

	template struct Matrix3x3<float32>;
	template struct Matrix3x3<float64>;
} // namespace rox::math
