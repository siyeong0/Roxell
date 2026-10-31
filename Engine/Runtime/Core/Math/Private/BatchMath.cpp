#include <BatchMath.h>
#include <Types.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Assertion.h>

#include "BatchMathSimd.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		void transformBatch(const AffineTransform<T>& transform, const Vector3<T>* source, Vector3<T>* destination, rox::core::usize count, bool positions) noexcept
		{
			ASSERT(transform.IsFinite(), "Batch affine transform must be finite.");
			ASSERT(count <= rox::core::NumericLimits<rox::core::usize>::max() / sizeof(Vector3<T>), "Batch affine range size must be representable.");
			ASSERT(count == 0 || (source != nullptr && destination != nullptr), "Nonempty batch affine ranges require valid storage.");
			const rox::core::uintptr sourceAddress = reinterpret_cast<rox::core::uintptr>(source);
			const rox::core::uintptr destinationAddress = reinterpret_cast<rox::core::uintptr>(destination);
			const rox::core::uintptr distance = sourceAddress > destinationAddress ? sourceAddress - destinationAddress : destinationAddress - sourceAddress;
			ASSERT(source == destination || distance >= count * sizeof(Vector3<T>), "Batch affine ranges must be disjoint or exactly in-place.");

			for (rox::core::usize index = 0; index < count; ++index)
			{
				ASSERT(source[index].IsFinite(), "Batch affine input vectors must be finite.");
			}

			// Snapshot permits the caller's transform to reside alongside destination storage.
			const AffineTransform<T> local = transform;
			rox::core::usize index = 0;
			const T offsets[3] = {positions ? local.Translation.X : T(0.0), positions ? local.Translation.Y : T(0.0), positions ? local.Translation.Z : T(0.0)};
			BatchPair<T> coefficients[3][3];
			BatchPair<T> translation[3];

			for (uint32 column = 0; column < 3; ++column)
			{
				translation[column] = broadcast(offsets[column]);

				for (uint32 row = 0; row < 3; ++row)
				{
					coefficients[row][column] = broadcast(local.Linear[row][column]);
				}
			}

			for (; count - index >= 2; index += 2)
			{
				const BatchPair<T> x = pair(source[index].X, source[index + 1].X);
				const BatchPair<T> y = pair(source[index].Y, source[index + 1].Y);
				const BatchPair<T> z = pair(source[index].Z, source[index + 1].Z);
				T output[3][2];

				for (uint32 column = 0; column < 3; ++column)
				{
					const BatchPair<T> xy = add(multiply(x, coefficients[0][column]), multiply(y, coefficients[1][column]));
					const BatchPair<T> xyz = add(xy, multiply(z, coefficients[2][column]));
					store(output[column], add(xyz, translation[column]));

					for (uint32 lane = 0; lane < 2; ++lane)
					{
						ASSERT(rox::core::IsFinite(output[column][lane]), "Batch affine result must be representable in the finite scalar range.");
					}
				}

				for (uint32 lane = 0; lane < 2; ++lane)
				{
					destination[index + lane] = {static_cast<T>(output[0][lane]), static_cast<T>(output[1][lane]), static_cast<T>(output[2][lane])};
				}
			}

			for (; index < count; ++index)
			{
				destination[index] = positions ? local.TransformPosition(source[index]) : local.TransformDirection(source[index]);
			}
		}
	} // namespace

	template <MathScalar T>
	void TransformPositions(const AffineTransform<T>& transform, const Vector3<T>* source, Vector3<T>* destination, rox::core::usize count) noexcept
	{
		transformBatch<T>(transform, source, destination, count, true);
	}

	template <MathScalar T>
	void TransformDirections(const AffineTransform<T>& transform, const Vector3<T>* source, Vector3<T>* destination, rox::core::usize count) noexcept
	{
		transformBatch<T>(transform, source, destination, count, false);
	}

	template <MathScalar T>
	void TransformNormals(const AffineTransform<T>& transform, const Vector3<T>* source, Vector3<T>* outNormals, rox::core::usize count) noexcept
	{
		TransformDirections<T>({transform.NormalMatrix(), Vector3<T>::Zero()}, source, outNormals, count);
	}

	namespace
	{
		bool disjoint(const void* first, rox::core::usize firstBytes, const void* second, rox::core::usize secondBytes) noexcept
		{
			const rox::core::uintptr a = reinterpret_cast<rox::core::uintptr>(first);
			const rox::core::uintptr b = reinterpret_cast<rox::core::uintptr>(second);
			return a <= b ? b - a >= firstBytes : a - b >= secondBytes;
		}

		template <MathScalar T>
		T narrowBatch(T value) noexcept
		{
			ASSERT(rox::core::IsFinite(value), "Batch result must be representable in the finite scalar range.");
			return static_cast<T>(value);
		}
	} // namespace

	template <MathScalar T>
	void TransformVectors(const Matrix4x4<T>& transform, const Vector4<T>* source, Vector4<T>* outVectors, rox::core::usize count) noexcept
	{
		ASSERT(transform.IsFinite(), "Batch matrix must be finite.");
		ASSERT(count <= rox::core::NumericLimits<rox::core::usize>::max() / sizeof(Vector4<T>), "Batch range must be representable.");
		ASSERT(count == 0 || (source != nullptr && outVectors != nullptr), "Nonempty batch requires valid storage.");
		ASSERT(source == outVectors || disjoint(source, count * sizeof(Vector4<T>), outVectors, count * sizeof(Vector4<T>)), "Batch vector ranges must be disjoint or exactly in-place.");

		for (rox::core::usize i = 0; i < count; ++i)
		{
			ASSERT(source[i].IsFinite(), "Batch input vectors must be finite.");
		}

		BatchPair<T> coefficients[4][4];

		for (uint32 row = 0; row < 4; ++row)
		{
			for (uint32 column = 0; column < 4; ++column)
			{
				coefficients[row][column] = broadcast(transform[row][column]);
			}
		}

		for (rox::core::usize i = 0; i < count; i += 2)
		{
			const Vector4<T>& first = source[i];
			const Vector4<T>& second = source[i + (count - i > 1 ? 1 : 0)];
			const BatchPair<T> input[] = {pair(first.X, second.X), pair(first.Y, second.Y), pair(first.Z, second.Z), pair(first.W, second.W)};
			T output[4][2];

			for (uint32 column = 0; column < 4; ++column)
			{
				BatchPair<T> value = multiply(input[0], coefficients[0][column]);

				for (uint32 row = 1; row < 4; ++row)
				{
					value = add(value, multiply(input[row], coefficients[row][column]));
				}

				store(output[column], value);
			}

			for (rox::core::usize lane = 0; lane < 2 && lane < count - i; ++lane)
			{
				outVectors[i + lane] = {narrowBatch<T>(output[0][lane]), narrowBatch<T>(output[1][lane]), narrowBatch<T>(output[2][lane]), narrowBatch<T>(output[3][lane])};
			}
		}
	}

	template <MathScalar T>
	void DotProducts(const Vector3<T>* left, const Vector3<T>* right, T* outProducts, rox::core::usize count) noexcept
	{
		ASSERT(count <= rox::core::NumericLimits<rox::core::usize>::max() / sizeof(Vector3<T>), "Batch range must be representable.");
		ASSERT(count == 0 || (left != nullptr && right != nullptr && outProducts != nullptr), "Nonempty batch requires valid storage.");
		ASSERT(disjoint(left, count * sizeof(Vector3<T>), outProducts, count * sizeof(T)) && disjoint(right, count * sizeof(Vector3<T>), outProducts, count * sizeof(T)), "Dot product output must not overlap inputs.");

		for (rox::core::usize i = 0; i < count; ++i)
		{
			ASSERT(left[i].IsFinite() && right[i].IsFinite(), "Batch input vectors must be finite.");
		}

		for (rox::core::usize i = 0; i < count; i += 2)
		{
			const rox::core::usize j = i + (count - i > 1 ? 1 : 0);
			const BatchPair<T> x = multiply(pair(left[i].X, left[j].X), pair(right[i].X, right[j].X));
			const BatchPair<T> y = multiply(pair(left[i].Y, left[j].Y), pair(right[i].Y, right[j].Y));
			const BatchPair<T> z = multiply(pair(left[i].Z, left[j].Z), pair(right[i].Z, right[j].Z));
			T values[2];
			store(values, add(add(x, y), z));
			outProducts[i] = narrowBatch<T>(values[0]);

			if (j != i)
			{
				outProducts[j] = narrowBatch<T>(values[1]);
			}
		}
	}

	template void TransformPositions(const AffineTransform<float32>& transform, const Vector3<float32>* source, Vector3<float32>* destination, rox::core::usize count) noexcept;
	template void TransformPositions(const AffineTransform<float64>& transform, const Vector3<float64>* source, Vector3<float64>* destination, rox::core::usize count) noexcept;
	template void TransformDirections(const AffineTransform<float32>& transform, const Vector3<float32>* source, Vector3<float32>* destination, rox::core::usize count) noexcept;
	template void TransformDirections(const AffineTransform<float64>& transform, const Vector3<float64>* source, Vector3<float64>* destination, rox::core::usize count) noexcept;
	template void TransformNormals(const AffineTransform<float32>& transform, const Vector3<float32>* source, Vector3<float32>* outNormals, rox::core::usize count) noexcept;
	template void TransformNormals(const AffineTransform<float64>& transform, const Vector3<float64>* source, Vector3<float64>* outNormals, rox::core::usize count) noexcept;
	template void TransformVectors(const Matrix4x4<float32>& transform, const Vector4<float32>* source, Vector4<float32>* outVectors, rox::core::usize count) noexcept;
	template void TransformVectors(const Matrix4x4<float64>& transform, const Vector4<float64>* source, Vector4<float64>* outVectors, rox::core::usize count) noexcept;
	template void DotProducts(const Vector3<float32>* left, const Vector3<float32>* right, float32* outProducts, rox::core::usize count) noexcept;
	template void DotProducts(const Vector3<float64>* left, const Vector3<float64>* right, float64* outProducts, rox::core::usize count) noexcept;
} // namespace rox::math
