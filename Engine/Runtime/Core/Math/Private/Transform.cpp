#include <Transform.h>
#include <Assertion.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Vocabulary.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		using TrsRow = rox::core::StaticArray<T, 3>;

		template <MathScalar T>
		T rowDot(const TrsRow<T>& a, const TrsRow<T>& b) noexcept
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		template <MathScalar T>
		TrsRow<T> rowCross(const TrsRow<T>& a, const TrsRow<T>& b) noexcept
		{
			return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
		}

		template <MathScalar T>
		TrsRow<T> unitRow(const TrsRow<T>& row) noexcept
		{
			const T length = rox::core::Hypot(row[0], row[1], row[2]);
			return {row[0] / length, row[1] / length, row[2] / length};
		}

		template <MathScalar T>
		bool hasValidComponents(const Transform<T>& transform) noexcept
		{
			return transform.Translation.IsFinite() && transform.Scale.IsFinite() && transform.Rotation.IsFinite() &&
			       (transform.Rotation.X != T(0.0) || transform.Rotation.Y != T(0.0) ||
			        transform.Rotation.Z != T(0.0) || transform.Rotation.W != T(0.0));
		}
	} // namespace

	template <MathScalar T>
	AffineTransform<T> Transform<T>::ToAffine() const noexcept
	{
		ASSERT(hasValidComponents<T>(*this), "Transform components must be finite with a nonzero rotation.");
		const Matrix3x3<T> rotation = Rotation.Normalized(T(0.0)).ToMatrix3x3();
		const T scales[3] = {Scale.X, Scale.Y, Scale.Z};
		Matrix3x3<T> linear;

		for (uint32 row = 0; row < 3; ++row)
		{
			for (uint32 column = 0; column < 3; ++column)
			{
				const T value = static_cast<T>(scales[row]) * rotation[row][column];
				ASSERT(rox::core::IsFinite(value), "Affine transform result must be representable in the finite scalar range.");
				linear[row][column] = static_cast<T>(value);
			}
		}

		return {linear, Translation};
	}

	template <MathScalar T>
	Vector3<T> Transform<T>::TransformNormal(const Vector3<T>& normal) const noexcept
	{
		ASSERT(normal.IsFinite(), "Affine transform inputs must be finite.");
		return ToAffine().TransformNormal(normal);
	}

	template <MathScalar T>
	Vector3<T> Transform<T>::InverseTransformPosition(const Vector3<T>& position) const noexcept
	{
		ASSERT(position.IsFinite(), "Affine transform inputs must be finite.");
		return ToAffine().InverseTransformPosition(position);
	}

	template <MathScalar T>
	Vector3<T> Transform<T>::InverseTransformDirection(const Vector3<T>& direction) const noexcept
	{
		ASSERT(direction.IsFinite(), "Affine transform inputs must be finite.");
		return ToAffine().InverseTransformDirection(direction);
	}

	template <MathScalar T>
	AffineTransform<T> Transform<T>::ComposeAffine(const Transform<T>& parent) const noexcept
	{
		ASSERT(hasValidComponents<T>(*this) && hasValidComponents<T>(parent), "Transform components must be finite with a nonzero rotation.");
		return ToAffine() * parent.ToAffine();
	}

	template <MathScalar T>
	AffineTransform<T> Transform<T>::RelativeAffine(const Transform<T>& parentWorld) const noexcept
	{
		ASSERT(hasValidComponents<T>(*this) && hasValidComponents<T>(parentWorld), "Transform components must be finite with a nonzero rotation.");
		return ToAffine().RelativeTo(parentWorld.ToAffine());
	}

	template <MathScalar T>
	bool Transform<T>::DecomposeTRS(const Matrix4x4<T>& matrix, Transform<T>* outTransform) noexcept
	{
		ASSERT(outTransform != nullptr, "TRS decomposition output must not be null.");

		if (!matrix.IsAffine())
		{
			return false;
		}

		TrsRow<T> rows[3]{};
		T lengths[3]{};
		T scales[3]{};
		uint32 activeCount = 0;

		for (uint32 row = 0; row < 3; ++row)
		{
			rows[row] = {matrix[row][0], matrix[row][1], matrix[row][2]};
			lengths[row] = rox::core::Hypot(rows[row][0], rows[row][1], rows[row][2]);

			if (lengths[row] > rox::core::NumericLimits<T>::max())
			{
				return false;
			}

			scales[row] = static_cast<T>(lengths[row]);

			if (lengths[row] != T(0.0))
			{
				rows[row] = unitRow<T>(rows[row]);
				++activeCount;
			}
		}

		for (uint32 row = 0; row < 3; ++row)
		{
			for (uint32 other = row + 1; other < 3; ++other)
			{
				if (lengths[row] != T(0.0) && lengths[other] != T(0.0) &&
				    rox::core::Abs(rowDot<T>(rows[row], rows[other])) > T(1e-5))
				{
					return false;
				}
			}
		}

		if (activeCount == 3 && rowDot<T>(rows[0], rowCross<T>(rows[1], rows[2])) < T(0.0))
		{
			uint32 axis = 0;

			for (uint32 index = 1; index < 3; ++index)
			{
				if (lengths[index] > lengths[axis])
				{
					axis = index;
				}
			}

			scales[axis] = -scales[axis];

			for (T& lane : rows[axis])
			{
				lane = -lane;
			}
		}

		if (activeCount == 2)
		{
			for (uint32 row = 0; row < 3; ++row)
			{
				if (lengths[row] == T(0.0))
				{
					rows[row] = unitRow<T>(rowCross<T>(rows[(row + 1) % 3], rows[(row + 2) % 3]));
				}
			}
		}

		if (activeCount == 1)
		{
			uint32 row = 0;

			while (lengths[row] == T(0.0))
			{
				++row;
			}

			uint32 axis = 0;

			for (uint32 index = 1; index < 3; ++index)
			{
				if (rox::core::Abs(rows[row][index]) < rox::core::Abs(rows[row][axis]))
				{
					axis = index;
				}
			}

			TrsRow<T> cardinal{};
			cardinal[axis] = T(1.0);
			rows[(row + 1) % 3] = unitRow<T>(rowCross<T>(rows[row], cardinal));
			rows[(row + 2) % 3] = rowCross<T>(rows[row], rows[(row + 1) % 3]);
		}

		Transform<T> candidate({matrix[3][0], matrix[3][1], matrix[3][2]}, Quaternion<T>::Identity(),
		                       {scales[0], scales[1], scales[2]});
		if (activeCount != 0)
		{
			Matrix3x3<T> basis;

			for (uint32 row = 0; row < 3; ++row)
			{
				for (uint32 column = 0; column < 3; ++column)
				{
					basis[row][column] = static_cast<T>(rows[row][column]);
				}
			}

			candidate.Rotation = Quaternion<T>::FromRotationMatrix(basis);
		}

		const Matrix3x3<T> rotation = candidate.Rotation.Normalized(T(0.0)).ToMatrix3x3();

		for (uint32 row = 0; row < 3; ++row)
		{
			for (uint32 column = 0; column < 3; ++column)
			{
				const T value = static_cast<T>(scales[row]) * rotation[row][column];

				if (!rox::core::IsFinite(value))
				{
					return false;
				}

				const T error = rox::core::Abs(static_cast<T>(static_cast<T>(value)) - matrix[row][column]);

				if (error > T(2e-5) * lengths[row] + T(2.0) * rox::core::NumericLimits<T>::denorm_min())
				{
					return false;
				}
			}
		}

		*outTransform = candidate;
		return true;
	}

	template <MathScalar T>
	Matrix4x4<T> Transform<T>::ToMatrix() const noexcept
	{
		return Matrix4x4<T>::FromTRS(Translation, Rotation, Scale);
	}

	template <MathScalar T>
	Vector3<T> Transform<T>::TransformPosition(const Vector3<T>& position) const noexcept
	{
		ASSERT(position.IsFinite(), "Affine transform inputs must be finite.");
		return ToAffine().TransformPosition(position);
	}

	template <MathScalar T>
	Vector3<T> Transform<T>::TransformDirection(const Vector3<T>& direction) const noexcept
	{
		ASSERT(direction.IsFinite(), "Affine transform inputs must be finite.");
		return ToAffine().TransformDirection(direction);
	}

	template struct Transform<float32>;
	template struct Transform<float64>;
} // namespace rox::math
