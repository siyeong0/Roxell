#include <AffineTransform.h>
#include <Assertion.h>
#include <Numeric.hpp>
#include <Comparison.hpp>
#include <Utility.hpp>

#include <cmath>
#include <limits>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		T narrow(T value) noexcept
		{
			ASSERT(core::IsFinite(value), "Affine transform result must be representable in the finite scalar range.");
			return static_cast<T>(value);
		}

		template <MathScalar T>
		struct InverseLinear final
		{
			T L[3][3];
		};

		template <MathScalar T>
		struct ExponentValue final
		{
			T Fraction;
			int32 Exponent;

			static ExponentValue From(T value, int32 exponent = 0) noexcept
			{
				int32 shift;
				const T fraction = std::frexp(value, &shift);
				return {fraction, value == T(0) ? 0 : exponent + shift};
			}

			ExponentValue operator*(ExponentValue other) const noexcept
			{
				T product = Fraction * other.Fraction;
				int32 exponent = Exponent + other.Exponent;
				if (product == T(0))
				{
					return {product, 0};
				}

				if (core::Abs(product) < T(0.5))
				{
					product *= T(2);
					--exponent;
				}

				return {product, exponent};
			}

			ExponentValue operator+(ExponentValue other) const noexcept
			{
				if (Fraction == T(0))
				{
					return other;
				}

				if (other.Fraction == T(0))
				{
					return *this;
				}

				const int32 exponent = core::Max(Exponent, other.Exponent);
				return From(std::ldexp(Fraction, Exponent - exponent) + std::ldexp(other.Fraction, other.Exponent - exponent), exponent);
			}

			ExponentValue operator-(ExponentValue other) const noexcept
			{
				other.Fraction = -other.Fraction;
				return *this + other;
			}

			T DividedBy(ExponentValue other) const noexcept
			{
				if (Fraction == T(0))
				{
					return Fraction;
				}

				return std::ldexp(Fraction / other.Fraction, Exponent - other.Exponent);
			}
		};

		template <MathScalar T>
		InverseLinear<T> invertWithExponents(const Matrix3x3<T>& matrix) noexcept
		{
			// Keep exponents separate until the final quotient so no cofactor is lost to range limits.
			using E = ExponentValue<T>;
			const E a = E::From(matrix[0][0]);
			const E b = E::From(matrix[0][1]);
			const E c = E::From(matrix[0][2]);
			const E d = E::From(matrix[1][0]);
			const E e = E::From(matrix[1][1]);
			const E f = E::From(matrix[1][2]);
			const E g = E::From(matrix[2][0]);
			const E h = E::From(matrix[2][1]);
			const E i = E::From(matrix[2][2]);
			const E cofactors[3][3]{{e * i - f * h, c * h - b * i, b * f - c * e},
			                        {f * g - d * i, a * i - c * g, c * d - a * f},
			                        {d * h - e * g, b * g - a * h, a * e - b * d}};
			E terms[]{a * cofactors[0][0], b * cofactors[1][0], c * cofactors[2][0]};
			// Cancel larger-exponent terms before adding a smaller determinant term.
			for (uint32 first = 0; first < 2; ++first)
			{
				for (uint32 second = first + 1; second < 3; ++second)
				{
					if (terms[first].Fraction == T(0) || (terms[second].Fraction != T(0) && terms[first].Exponent < terms[second].Exponent))
					{
						core::Swap(terms[first], terms[second]);
					}
				}
			}

			const E determinant = terms[0] + terms[1] + terms[2];
			ASSERT(determinant.Fraction != T(0), "Affine transform linear matrix must be invertible.");
			InverseLinear<T> result;
			for (uint32 row = 0; row < 3; ++row)
			{
				for (uint32 column = 0; column < 3; ++column)
				{
					result.L[row][column] = cofactors[row][column].DividedBy(determinant);
				}
			}

			return result;
		}

		template <MathScalar T>
		InverseLinear<T> invert(const Matrix3x3<T>& matrix) noexcept
		{
			const T a = matrix[0][0];
			const T b = matrix[0][1];
			const T c = matrix[0][2];
			const T d = matrix[1][0];
			const T e = matrix[1][1];
			const T f = matrix[1][2];
			const T g = matrix[2][0];
			const T h = matrix[2][1];
			const T i = matrix[2][2];
			InverseLinear<T> result{{{e * i - f * h, c * h - b * i, b * f - c * e},
			                         {f * g - d * i, a * i - c * g, c * d - a * f},
			                         {d * h - e * g, b * g - a * h, a * e - b * d}}};
			const T determinant = a * result.L[0][0] + b * result.L[1][0] + c * result.L[2][0];
			// Nonzero entries must leave normal-range headroom for two-factor cofactor products.
			constexpr T small = sizeof(T) == sizeof(float32) ? T(0x1p-63) : T(0x1p-511);
			bool unsafeProducts = false;
			for (uint32 index = 0; index < 9; ++index)
			{
				if constexpr (sizeof(T) == sizeof(float32))
				{
					const uint32 bits = core::BitCast<uint32>(matrix.Data()[index]) & 0x7fffffffu;
					constexpr uint32 lower = core::BitCast<uint32>(small);
					constexpr uint32 upper = core::BitCast<uint32>(T(1) / small);
					unsafeProducts |= (bits != 0) & (bits - lower > upper - lower);
				}
				else
				{
					const uint64 bits = core::BitCast<uint64>(matrix.Data()[index]) & 0x7fffffffffffffffull;
					constexpr uint64 lower = core::BitCast<uint64>(small);
					constexpr uint64 upper = core::BitCast<uint64>(T(1) / small);
					unsafeProducts |= (bits != 0) & (bits - lower > upper - lower);
				}
			}

			const T determinantMagnitude = core::Abs(determinant);
			if (unsafeProducts || !(determinantMagnitude >= small && determinantMagnitude <= T(1) / small))
			{
				ASSERT(matrix.IsFinite(), "Affine transform inputs must be finite.");
				return invertWithExponents<T>(matrix);
			}

			const T inverseDeterminant = T(1) / determinant;
			for (uint32 row = 0; row < 3; ++row)
			{
				for (uint32 column = 0; column < 3; ++column)
				{
					result.L[row][column] *= inverseDeterminant;
				}
			}

			return result;
		}

		template <MathScalar T>
		Vector3<T> map(const InverseLinear<T>& linear, T x, T y, T z) noexcept
		{
			return {narrow<T>(x * linear.L[0][0] + y * linear.L[1][0] + z * linear.L[2][0]),
			        narrow<T>(x * linear.L[0][1] + y * linear.L[1][1] + z * linear.L[2][1]),
			        narrow<T>(x * linear.L[0][2] + y * linear.L[1][2] + z * linear.L[2][2])};
		}

		template <MathScalar T>
		Vector3<T> map(const Matrix3x3<T>& linear, const Vector3<T>& value, const Vector3<T>& translation) noexcept
		{
			T result[3];
			const T offset[3] = {translation.X, translation.Y, translation.Z};
			for (uint32 column = 0; column < 3; ++column)
			{
				result[column] = narrow<T>(static_cast<T>(value.X) * linear[0][column] +
				                           static_cast<T>(value.Y) * linear[1][column] +
				                           static_cast<T>(value.Z) * linear[2][column] + offset[column]);
			}

			return {result[0], result[1], result[2]};
		}
	} // namespace

	template <MathScalar T>
	bool AffineTransform<T>::IsFinite() const noexcept
	{
		return Linear.IsFinite() && Translation.IsFinite();
	}

	template <MathScalar T>
	AffineTransform<T> AffineTransform<T>::FromMatrix(const Matrix4x4<T>& matrix) noexcept
	{
		ASSERT(matrix.IsAffine(), "Matrix must be finite and affine.");
		return {matrix.ExtractLinearMatrix(), matrix.ExtractTranslation()};
	}

	template <MathScalar T>
	Matrix4x4<T> AffineTransform<T>::ToMatrix() const noexcept
	{
		ASSERT(IsFinite(), "Affine transform inputs must be finite.");
		return {Linear[0][0],  Linear[0][1],  Linear[0][2],  T(0.0), 
				Linear[1][0],  Linear[1][1],  Linear[1][2],  T(0.0),
		        Linear[2][0],  Linear[2][1],  Linear[2][2],  T(0.0), 
				Translation.X, Translation.Y, Translation.Z, T(1.0)};
	}

	template <MathScalar T>
	Vector3<T> AffineTransform<T>::TransformPosition(const Vector3<T>& position) const noexcept
	{
		ASSERT(IsFinite() && position.IsFinite(), "Affine transform inputs must be finite.");
		return map<T>(Linear, position, Translation);
	}

	template <MathScalar T>
	Vector3<T> AffineTransform<T>::TransformDirection(const Vector3<T>& direction) const noexcept
	{
		ASSERT(IsFinite() && direction.IsFinite(), "Affine transform inputs must be finite.");
		return map<T>(Linear, direction, Vector3<T>::Zero());
	}

	template <MathScalar T>
	AffineTransform<T> AffineTransform<T>::Inversed() const noexcept
	{
		ASSERT(Translation.IsFinite(), "Affine transform inputs must be finite.");
		const InverseLinear<T> inverse = invert<T>(Linear);
		AffineTransform<T> result;

		for (uint32 row = 0; row < 3; ++row)
		{
			for (uint32 column = 0; column < 3; ++column)
			{
				result.Linear[row][column] = narrow<T>(inverse.L[row][column]);
			}
		}

		result.Translation = map<T>(inverse, -static_cast<T>(Translation.X), -static_cast<T>(Translation.Y),
		                            -static_cast<T>(Translation.Z));
		return result;
	}

	template <MathScalar T>
	Vector3<T> AffineTransform<T>::InverseTransformPosition(const Vector3<T>& position) const noexcept
	{
		ASSERT(Translation.IsFinite() && position.IsFinite(), "Affine transform inputs must be finite.");
		return map<T>(invert<T>(Linear), static_cast<T>(position.X) - Translation.X, static_cast<T>(position.Y) - Translation.Y, static_cast<T>(position.Z) - Translation.Z);
	}

	template <MathScalar T>
	Vector3<T> AffineTransform<T>::InverseTransformDirection(const Vector3<T>& direction) const noexcept
	{
		ASSERT(Translation.IsFinite() && direction.IsFinite(), "Affine transform inputs must be finite.");
		return map<T>(invert<T>(Linear), direction.X, direction.Y, direction.Z);
	}

	template <MathScalar T>
	AffineTransform<T> AffineTransform<T>::operator*(const AffineTransform<T>& next) const noexcept
	{
		ASSERT(IsFinite() && next.IsFinite(), "Affine transform inputs must be finite.");
		AffineTransform<T> result;
		result.Linear = Matrix3x3<T>::FromRows(map<T>(next.Linear, Linear.GetRow(0), Vector3<T>::Zero()),
		                                       map<T>(next.Linear, Linear.GetRow(1), Vector3<T>::Zero()),
		                                       map<T>(next.Linear, Linear.GetRow(2), Vector3<T>::Zero()));
		result.Translation = map<T>(next.Linear, Translation, next.Translation);
		return result;
	}

	template <MathScalar T>
	Matrix3x3<T> AffineTransform<T>::NormalMatrix() const noexcept
	{
		ASSERT(Translation.IsFinite(), "Affine transform inputs must be finite.");
		const InverseLinear<T> inverse = invert<T>(Linear);
		Matrix3x3<T> result;

		for (uint32 row = 0; row < 3; ++row)
		{
			for (uint32 column = 0; column < 3; ++column)
			{
				result[row][column] = narrow<T>(inverse.L[column][row]);
			}
		}

		return result;
	}

	template <MathScalar T>
	Vector3<T> AffineTransform<T>::TransformNormal(const Vector3<T>& normal) const noexcept
	{
		ASSERT(Translation.IsFinite() && normal.IsFinite(), "Affine transform inputs must be finite.");
		const InverseLinear<T> inverse = invert<T>(Linear);
		return {narrow<T>(normal.X * inverse.L[0][0] + normal.Y * inverse.L[0][1] + normal.Z * inverse.L[0][2]),
		        narrow<T>(normal.X * inverse.L[1][0] + normal.Y * inverse.L[1][1] + normal.Z * inverse.L[1][2]),
		        narrow<T>(normal.X * inverse.L[2][0] + normal.Y * inverse.L[2][1] + normal.Z * inverse.L[2][2])};
	}

	template <MathScalar T>
	AffineTransform<T> AffineTransform<T>::RelativeTo(const AffineTransform<T>& parentWorld) const noexcept
	{
		ASSERT(IsFinite() && parentWorld.IsFinite(), "Affine transform inputs must be finite.");
		return *this * parentWorld.Inversed();
	}

	template struct AffineTransform<float32>;
	template struct AffineTransform<float64>;
} // namespace rox::math
