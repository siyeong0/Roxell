#include <AffineTransform2D.h>
#include <Assertion.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

#include <cmath>
#include <limits>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		T narrowAffine2DLane(T value) noexcept
		{
			ASSERT(rox::core::IsFinite(value), "2D affine result must be representable in the finite scalar range.");
			return static_cast<T>(value);
		}

		template <MathScalar T>
		struct InverseLinear2
		{
			T XX;
			T XY;
			T YX;
			T YY;
		};

		template <MathScalar T>
		InverseLinear2<T> inverseLinear2DScaled(const Vector2<T>& axisX, const Vector2<T>& axisY) noexcept
		{
			const T firstScale = core::Max(core::Abs(axisX.X), core::Abs(axisX.Y));
			const T secondScale = core::Max(core::Abs(axisY.X), core::Abs(axisY.Y));
			ASSERT(firstScale != T(0) && secondScale != T(0), "2D affine linear map must be invertible.");

			const T a = axisX.X / firstScale;
			const T b = axisX.Y / firstScale;
			const T c = axisY.X / secondScale;
			const T d = axisY.Y / secondScale;
			const T determinant = a * d - b * c;
			ASSERT(core::IsFinite(determinant) && determinant != T(0.0), "2D affine linear map must be invertible.");
			return {(d / determinant) / firstScale, (-b / determinant) / secondScale, (-c / determinant) / firstScale, (a / determinant) / secondScale};
		}

		template <MathScalar T>
		InverseLinear2<T> inverseLinear2D(const Vector2<T>& axisX, const Vector2<T>& axisY) noexcept
		{
			const T a = axisX.X;
			const T b = axisX.Y;
			const T c = axisY.X;
			const T d = axisY.Y;
			const T determinant = a * d - b * c;
			if (!core::IsFinite(determinant) || core::Abs(determinant) < core::NumericLimits<T>::min())
			{
				ASSERT(axisX.IsFinite() && axisY.IsFinite(), "2D affine inputs must be finite.");
				return inverseLinear2DScaled<T>(axisX, axisY);
			}

			// A finite, normal determinant establishes finite axes and invertibility.
			return {d / determinant, -b / determinant, -c / determinant, a / determinant};
		}

		template <MathScalar T>
		Vector2<T> mapLinear2D(const InverseLinear2<T>& linear, T x, T y) noexcept
		{
			return {narrowAffine2DLane<T>(x * linear.XX + y * linear.YX), narrowAffine2DLane<T>(x * linear.XY + y * linear.YY)};
		}
	} // namespace

	template <MathScalar T>
	bool AffineTransform2D<T>::IsFinite() const noexcept
	{
		return AxisX.IsFinite() && AxisY.IsFinite() && Translation.IsFinite();
	}

	template <MathScalar T>
	AffineTransform2D<T> AffineTransform2D<T>::FromTRS(const Vector2<T>& translation, T radians, const Vector2<T>& scale) noexcept
	{
		ASSERT(translation.IsFinite() && rox::core::IsFinite(radians) && scale.IsFinite(), "2D affine inputs must be finite.");
		const T cosine = rox::core::Cos(static_cast<T>(radians));
		const T sine = rox::core::Sin(static_cast<T>(radians));
		return {{narrowAffine2DLane<T>(scale.X * cosine), narrowAffine2DLane<T>(scale.X * sine)},
		        {narrowAffine2DLane<T>(-scale.Y * sine), narrowAffine2DLane<T>(scale.Y * cosine)},
		        translation};
	}

	template <MathScalar T>
	AffineTransform2D<T> AffineTransform2D<T>::FromMatrix(const Matrix3x3<T>& matrix) noexcept
	{
		ASSERT(matrix.IsFinite() && matrix[0][2] == T(0.0) && matrix[1][2] == T(0.0) && matrix[2][2] == T(1.0), "Matrix must be finite and 2D affine.");
		return {{matrix[0][0], matrix[0][1]}, {matrix[1][0], matrix[1][1]}, {matrix[2][0], matrix[2][1]}};
	}

	template <MathScalar T>
	Matrix3x3<T> AffineTransform2D<T>::ToMatrix() const noexcept
	{
		ASSERT(IsFinite(), "2D affine inputs must be finite.");
		return {AxisX.X, AxisX.Y, T(0.0), AxisY.X, AxisY.Y, T(0.0), Translation.X, Translation.Y, T(1.0)};
	}

	template <MathScalar T>
	Vector2<T> AffineTransform2D<T>::TransformPosition(const Vector2<T>& position) const noexcept
	{
		ASSERT(IsFinite() && position.IsFinite(), "2D affine inputs must be finite.");
		return {narrowAffine2DLane<T>(static_cast<T>(position.X) * AxisX.X + static_cast<T>(position.Y) * AxisY.X + Translation.X),
		        narrowAffine2DLane<T>(static_cast<T>(position.X) * AxisX.Y + static_cast<T>(position.Y) * AxisY.Y + Translation.Y)};
	}

	template <MathScalar T>
	Vector2<T> AffineTransform2D<T>::TransformDirection(const Vector2<T>& direction) const noexcept
	{
		ASSERT(IsFinite() && direction.IsFinite(), "2D affine inputs must be finite.");
		return {narrowAffine2DLane<T>(static_cast<T>(direction.X) * AxisX.X + static_cast<T>(direction.Y) * AxisY.X),
		        narrowAffine2DLane<T>(static_cast<T>(direction.X) * AxisX.Y + static_cast<T>(direction.Y) * AxisY.Y)};
	}

	template <MathScalar T>
	AffineTransform2D<T> AffineTransform2D<T>::operator*(const AffineTransform2D<T>& next) const noexcept
	{
		ASSERT(IsFinite() && next.IsFinite(), "2D affine inputs must be finite.");
		return {next.TransformDirection(AxisX), next.TransformDirection(AxisY), next.TransformPosition(Translation)};
	}

	template <MathScalar T>
	AffineTransform2D<T> AffineTransform2D<T>::Inversed() const noexcept
	{
		ASSERT(Translation.IsFinite(), "2D affine inputs must be finite.");
		const InverseLinear2<T> inverse = inverseLinear2D<T>(AxisX, AxisY);
		return {{narrowAffine2DLane<T>(inverse.XX), narrowAffine2DLane<T>(inverse.XY)},
		        {narrowAffine2DLane<T>(inverse.YX), narrowAffine2DLane<T>(inverse.YY)},
		        mapLinear2D<T>(inverse, -static_cast<T>(Translation.X), -static_cast<T>(Translation.Y))};
	}

	template <MathScalar T>
	Vector2<T> AffineTransform2D<T>::InverseTransformPosition(const Vector2<T>& position) const noexcept
	{
		ASSERT(Translation.IsFinite() && position.IsFinite(), "2D affine inputs must be finite.");
		const InverseLinear2<T> inverse = inverseLinear2D<T>(AxisX, AxisY);
		return mapLinear2D<T>(inverse, static_cast<T>(position.X) - Translation.X, static_cast<T>(position.Y) - Translation.Y);
	}

	template <MathScalar T>
	Vector2<T> AffineTransform2D<T>::InverseTransformDirection(const Vector2<T>& direction) const noexcept
	{
		ASSERT(Translation.IsFinite() && direction.IsFinite(), "2D affine inputs must be finite.");
		return mapLinear2D<T>(inverseLinear2D<T>(AxisX, AxisY), direction.X, direction.Y);
	}

	template <MathScalar T>
	Vector2<T> AffineTransform2D<T>::TransformNormal(const Vector2<T>& normal) const noexcept
	{
		ASSERT(Translation.IsFinite() && normal.IsFinite(), "2D affine inputs must be finite.");
		const InverseLinear2<T> inverse = inverseLinear2D<T>(AxisX, AxisY);
		return mapLinear2D<T>({inverse.XX, inverse.YX, inverse.XY, inverse.YY}, normal.X, normal.Y);
	}

	template <MathScalar T>
	AffineTransform2D<T> AffineTransform2D<T>::RelativeTo(const AffineTransform2D<T>& parentWorld) const noexcept
	{
		ASSERT(IsFinite() && parentWorld.IsFinite(), "2D affine inputs must be finite.");
		return *this * parentWorld.Inversed();
	}

	template struct AffineTransform2D<float32>;
	template struct AffineTransform2D<float64>;
} // namespace rox::math
