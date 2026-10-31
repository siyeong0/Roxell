#include <Matrix4x4.hpp>
#include <MathConstants.hpp>
#include <Assertion.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Utility.hpp>
#include <Quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		[[nodiscard]] T finiteScalar(T value) noexcept
		{
			ASSERT(rox::core::IsFinite(value), "Matrix calculation must produce a finite value.");
			ASSERT(rox::core::Abs(value) <= static_cast<T>(rox::core::NumericLimits<T>::max()), "Matrix calculation must fit in the scalar type.");
			return static_cast<T>(value);
		}
	} // namespace

	template <MathScalar T>
	T Matrix4x4<T>::Determinant() const noexcept
	{
		ASSERT(IsFinite(), "Matrix input must be finite.");
		T values[16];

		for (uint32 index = 0; index < 16; ++index)
		{
			values[index] = M.Values[index];
		}

		T determinant = T(0.0);

		for (uint32 column = 0; column < 4; ++column)
		{
			T minor[9];
			uint32 index = 0;

			for (uint32 row = 1; row < 4; ++row)
			{
				for (uint32 lane = 0; lane < 4; ++lane)
				{
					if (lane != column)
					{
						minor[index++] = values[row * 4 + lane];
					}
				}
			}

			const T cofactor = minor[0] * (minor[4] * minor[8] - minor[5] * minor[7]) -
			                   minor[1] * (minor[3] * minor[8] - minor[5] * minor[6]) +
			                   minor[2] * (minor[3] * minor[7] - minor[4] * minor[6]);
			determinant += (column % 2 == 0 ? values[column] : -values[column]) * cofactor;
		}

		return determinant;
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::Translation(const Vector3<T>& translation) noexcept
	{
		Matrix4x4<T> result;
		result.M[3][0] = translation.X;
		result.M[3][1] = translation.Y;
		result.M[3][2] = translation.Z;
		return result;
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::Scale(const Vector3<T>& scale) noexcept
	{
		return {scale.X, T(0.0),  T(0.0),  T(0.0), 
				T(0.0),  scale.Y, T(0.0),  T(0.0),
		        T(0.0),  T(0.0),  scale.Z, T(0.0), 
				T(0.0),  T(0.0),  T(0.0),  T(1.0)};
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::RotationX(T radians) noexcept
	{
		const T cosine = rox::core::Cos(radians);
		const T sine = rox::core::Sin(radians);
		return {T(1.0), T(0.0), T(0.0), T(0.0), 
				T(0.0), cosine, sine,   T(0.0),
		        T(0.0), -sine,  cosine, T(0.0), 
				T(0.0), T(0.0), T(0.0), T(1.0)};
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::RotationY(T radians) noexcept
	{
		const T cosine = rox::core::Cos(radians);
		const T sine = rox::core::Sin(radians);
		return {cosine, T(0.0), -sine,  T(0.0), 
				T(0.0), T(1.0), T(0.0), T(0.0),
		        sine,   T(0.0), cosine, T(0.0), 
				T(0.0), T(0.0), T(0.0), T(1.0)};
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::RotationZ(T radians) noexcept
	{
		const T cosine = rox::core::Cos(radians);
		const T sine = rox::core::Sin(radians);
		return {cosine, sine,   T(0.0), T(0.0), 
				-sine,  cosine, T(0.0), T(0.0),
		        T(0.0), T(0.0), T(1.0), T(0.0), 
				T(0.0), T(0.0), T(0.0), T(1.0)};
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::RotationAxis(const Vector3<T>& axis, T radians) noexcept
	{
		const Matrix3x3<T> rotation = Matrix3x3<T>::RotationAxis(axis, radians);
		return {rotation.M[0][0], rotation.M[0][1], rotation.M[0][2], T(0.0),
		        rotation.M[1][0], rotation.M[1][1], rotation.M[1][2], T(0.0),
		        rotation.M[2][0], rotation.M[2][1], rotation.M[2][2], T(0.0),
		        T(0.0),           T(0.0),           T(0.0),           T(1.0)};
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::FromTRS(const Vector3<T>& translation, const Quaternion<T>& rotation, const Vector3<T>& scale) noexcept
	{
		return Scale(scale) * rotation.ToMatrix4x4() * Translation(translation);
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::LookAtLH(const Vector3<T>& eye, const Vector3<T>& target, const Vector3<T>& up) noexcept
	{
		const Vector3<T> forward = (target - eye).Normalized();
		const Vector3<T> right = up.Cross(forward).Normalized();
		const Vector3<T> correctedUp = forward.Cross(right);
		return {right.X,         correctedUp.X,         forward.X,         T(0.0),
		        right.Y,         correctedUp.Y,         forward.Y,         T(0.0),
		        right.Z,         correctedUp.Z,         forward.Z,         T(0.0),
		        -eye.Dot(right), -eye.Dot(correctedUp), -eye.Dot(forward), T(1.0)};
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::PerspectiveFovLH(T verticalFovRadians, T aspectRatio, T nearPlane, T farPlane) noexcept
	{
		ASSERT(rox::core::IsFinite(verticalFovRadians), "Perspective field of view must be finite.");
		ASSERT(rox::core::IsFinite(aspectRatio), "Perspective aspect ratio must be finite.");
		ASSERT(rox::core::IsFinite(nearPlane), "Perspective near plane must be finite.");
		ASSERT(rox::core::IsFinite(farPlane), "Perspective far plane must be finite.");
		ASSERT(verticalFovRadians > T(0.0) && verticalFovRadians < rox::core::PI<T>, "Perspective field of view must be between zero and pi radians.");
		ASSERT(aspectRatio > T(0.0), "Perspective aspect ratio must be positive.");
		ASSERT(nearPlane > T(0.0), "Perspective near plane must be positive.");
		ASSERT(farPlane > nearPlane, "Perspective far plane must be farther than the near plane.");

		const T yScale = T(1.0) / rox::core::Tan(static_cast<T>(verticalFovRadians) * T(0.5));
		const T xScale = yScale / static_cast<T>(aspectRatio);
		const T depthRange = static_cast<T>(farPlane) - static_cast<T>(nearPlane);
		const T depthScale = static_cast<T>(farPlane) / depthRange;
		const T depthOffset = -static_cast<T>(nearPlane) * depthScale;
		return {finiteScalar<T>(xScale), T(0.0), 				  T(0.0), 						T(0.0),
		        T(0.0), 				 finiteScalar<T>(yScale), T(0.0), 						T(0.0),
		        T(0.0), 				 T(0.0), 				  finiteScalar<T>(depthScale),	T(1.0),
		        T(0.0), 				 T(0.0), 				  finiteScalar<T>(depthOffset), T(0.0)};
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::OrthographicOffCenterLH(T left, T right, T bottom, T top, T nearPlane, T farPlane) noexcept
	{
		ASSERT(rox::core::IsFinite(left) && rox::core::IsFinite(right), "Orthographic horizontal bounds must be finite.");
		ASSERT(rox::core::IsFinite(bottom) && rox::core::IsFinite(top), "Orthographic vertical bounds must be finite.");
		ASSERT(rox::core::IsFinite(nearPlane) && rox::core::IsFinite(farPlane), "Orthographic depth bounds must be finite.");
		ASSERT(right != left, "Orthographic horizontal bounds must span a non-zero range.");
		ASSERT(top != bottom, "Orthographic vertical bounds must span a non-zero range.");
		ASSERT(farPlane > nearPlane, "Orthographic far plane must be farther than the near plane.");

		const T horizontalRange = static_cast<T>(right) - static_cast<T>(left);
		const T verticalRange = static_cast<T>(top) - static_cast<T>(bottom);
		const T depthRange = static_cast<T>(farPlane) - static_cast<T>(nearPlane);

		const T horizontalScale = finiteScalar<T>(T(2.0) / horizontalRange);
		const T verticalScale = finiteScalar<T>(T(2.0) / verticalRange);
		const T depthScale = finiteScalar<T>(T(1.0) / depthRange);

		const T horizontalOffset = finiteScalar<T>(-(right / horizontalRange + left / horizontalRange));
		const T verticalOffset = finiteScalar<T>(-(top / verticalRange + bottom / verticalRange));
		const T depthOffset = finiteScalar<T>(-static_cast<T>(nearPlane) / depthRange);

		return {horizontalScale,  T(0.0),         T(0.0),      T(0.0),
		        T(0.0),           verticalScale,  T(0.0),      T(0.0),
		        T(0.0),           T(0.0),         depthScale,  T(0.0),
		        horizontalOffset, verticalOffset, depthOffset, T(1.0)};
	}

	template <MathScalar T>
	Vector4<T> Matrix4x4<T>::TransformVector4(const Vector4<T>& vector) const noexcept
	{
		return {vector.X * M[0][0] + vector.Y * M[1][0] + vector.Z * M[2][0] + vector.W * M[3][0],
		        vector.X * M[0][1] + vector.Y * M[1][1] + vector.Z * M[2][1] + vector.W * M[3][1],
		        vector.X * M[0][2] + vector.Y * M[1][2] + vector.Z * M[2][2] + vector.W * M[3][2],
		        vector.X * M[0][3] + vector.Y * M[1][3] + vector.Z * M[2][3] + vector.W * M[3][3]};
	}

	template <MathScalar T>
	Vector3<T> Matrix4x4<T>::TransformPosition(const Vector3<T>& position, T epsilon) const noexcept
	{
		ASSERT(rox::core::IsFinite(epsilon) && epsilon >= T(0.0), "Position transform epsilon must be finite and non-negative.");
		const Vector4<T> transformed = TransformVector4(Vector4<T>(position, T(1.0)));
		ASSERT(rox::core::IsFinite(transformed.W) && rox::core::Abs(transformed.W) > epsilon, "Position transform must produce a usable homogeneous coordinate.");
		return transformed.XYZ() / transformed.W;
	}

	template <MathScalar T>
	Vector3<T> Matrix4x4<T>::TransformDirection(const Vector3<T>& direction) const noexcept
	{
		return TransformVector4(Vector4<T>(direction, T(0.0))).XYZ();
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::Transposed() const noexcept
	{
		return {M[0][0], M[1][0], M[2][0], M[3][0],
		        M[0][1], M[1][1], M[2][1], M[3][1],
		        M[0][2], M[1][2], M[2][2], M[3][2],
		        M[0][3], M[1][3], M[2][3], M[3][3]};
	}

	template <MathScalar T>
	bool Matrix4x4<T>::TryInverse(Matrix4x4<T>* outInverse, T epsilon) const noexcept
	{
		ASSERT(outInverse != nullptr, "Inverse Matrix4x4 output must not be null.");
		ASSERT(rox::core::IsFinite(epsilon) && epsilon >= T(0.0), "Matrix4x4 inversion epsilon must be finite and non-negative.");

		T maximumMagnitude = T(0.0);

		for (uint32 index = 0; index < 16; ++index)
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

		const T inverseScale = T(1.0) / maximumMagnitude;

		// Eliminate the scaled input, then rescale the inverse in the selected scalar type.
		T augmented[4][8]{};
		for (uint32 row = 0; row < 4; ++row)
		{
			for (uint32 column = 0; column < 4; ++column)
			{
				augmented[row][column] = M[row][column] * inverseScale;
			}

			augmented[row][row + 4] = T(1.0);
		}

		for (uint32 pivotColumn = 0; pivotColumn < 4; ++pivotColumn)
		{
			uint32 pivotRow = pivotColumn;
			T pivotMagnitude = rox::core::Abs(augmented[pivotRow][pivotColumn]);

			for (uint32 row = pivotColumn + 1; row < 4; ++row)
			{
				const T magnitude = rox::core::Abs(augmented[row][pivotColumn]);

				if (magnitude > pivotMagnitude)
				{
					pivotMagnitude = magnitude;
					pivotRow = row;
				}
			}

			if (!rox::core::IsFinite(pivotMagnitude) || pivotMagnitude <= epsilon)
			{
				return false;
			}

			if (pivotRow != pivotColumn)
			{
				for (uint32 column = 0; column < 8; ++column)
				{
					rox::core::Swap(augmented[pivotRow][column], augmented[pivotColumn][column]);
				}
			}

			const T pivot = augmented[pivotColumn][pivotColumn];

			for (uint32 column = 0; column < 8; ++column)
			{
				augmented[pivotColumn][column] /= pivot;
			}

			for (uint32 row = 0; row < 4; ++row)
			{
				if (row == pivotColumn)
				{
					continue;
				}

				const T factor = augmented[row][pivotColumn];

				for (uint32 column = 0; column < 8; ++column)
				{
					augmented[row][column] -= factor * augmented[pivotColumn][column];
				}
			}
		}

		Matrix4x4<T> inverse = Zero();

		for (uint32 row = 0; row < 4; ++row)
		{
			for (uint32 column = 0; column < 4; ++column)
			{
				const T value = augmented[row][column + 4] * inverseScale;

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
	Matrix4x4<T> Matrix4x4<T>::Inversed(T epsilon) const noexcept
	{
		Matrix4x4<T> result;
		const bool bInverted = TryInverse(&result, epsilon);
		ASSERT(bInverted, "Matrix4x4 must be finite and invertible.");
		return result;
	}

	template <MathScalar T>
	Matrix4x4<T> Matrix4x4<T>::operator*(const Matrix4x4<T>& other) const noexcept
	{
		Matrix4x4<T> result = Zero();

		for (uint32 row = 0; row < 4; ++row)
		{
			T* destination = result.M.Values + row * 4;
			const T* source = M.Values + row * 4;
			// Traverse adjacent columns while preserving each lane's original accumulation order.
			for (uint32 index = 0; index < 4; ++index)
			{
				const T* right = other.M.Values + index * 4;
				for (uint32 column = 0; column < 4; ++column)
				{
					destination[column] += source[index] * right[column];
				}
			}
		}

		return result;
	}

	template struct Matrix4x4<float32>;
	template struct Matrix4x4<float64>;
} // namespace rox::math
