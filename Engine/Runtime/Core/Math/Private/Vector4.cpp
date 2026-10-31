#include <Vector4.hpp>
#include <Numeric.hpp>
#include <Comparison.hpp>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	template <MathScalar T>
	T Vector4<T>::Length() const noexcept
	{
		const T xyLength = rox::core::Hypot(static_cast<T>(X), static_cast<T>(Y));
		const T zwLength = rox::core::Hypot(static_cast<T>(Z), static_cast<T>(W));
		return static_cast<T>(rox::core::Hypot(xyLength, zwLength));
	}

	template <MathScalar T>
	bool Vector4<T>::TryNormalize(Vector4<T>* outNormalized, T epsilon) const noexcept
	{
		ASSERT(outNormalized != nullptr, "Normalized vector output must not be null.");
		ASSERT(rox::core::IsFinite(epsilon) && epsilon >= T(0), "Normalization epsilon must be finite and nonnegative.");
		if (!IsFinite())
		{
			return false;
		}

		const T scale = rox::core::Max({rox::core::Abs(X), rox::core::Abs(Y), rox::core::Abs(Z), rox::core::Abs(W)});
		if (scale == T(0))
		{
			return false;
		}

		const Vector4<T> scaled{X / scale, Y / scale, Z / scale, W / scale};
		const T length = rox::core::Hypot(rox::core::Hypot(scaled.X, scaled.Y), rox::core::Hypot(scaled.Z, scaled.W));
		if (scale <= epsilon / length)
		{
			return false;
		}

		*outNormalized = scaled / length;
		return true;
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::Normalized(T epsilon) const noexcept
	{
		Vector4<T> result;
		const bool bNormalized = TryNormalize(&result, epsilon);
		ASSERT(bNormalized, "Vector4 must be finite and longer than the normalization epsilon.");
		return result;
	}

	template <MathScalar T>
	void Vector4<T>::Normalize(T epsilon) noexcept
	{
		*this = Normalized(epsilon);
	}

	template <MathScalar T>
	T Vector4<T>::Distance(const Vector4<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector operands must be finite.");
		const T deltaX = X - other.X;
		const T deltaY = Y - other.Y;
		const T deltaZ = Z - other.Z;
		const T deltaW = W - other.W;
		return rox::core::Hypot(rox::core::Hypot(deltaX, deltaY), rox::core::Hypot(deltaZ, deltaW));
	}

	template <MathScalar T>
	T Vector4<T>::DistanceSquared(const Vector4<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector4 operands must be finite.");
		const T deltaX = static_cast<T>(X) - other.X;
		const T deltaY = static_cast<T>(Y) - other.Y;
		const T deltaZ = static_cast<T>(Z) - other.Z;
		const T deltaW = static_cast<T>(W) - other.W;
		return static_cast<T>(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ + deltaW * deltaW);
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::Min(const Vector4<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector4 operands must be finite.");
		return {rox::core::Min(X, other.X), rox::core::Min(Y, other.Y), rox::core::Min(Z, other.Z),
		        rox::core::Min(W, other.W)};
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::Max(const Vector4<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector4 operands must be finite.");
		return {rox::core::Max(X, other.X), rox::core::Max(Y, other.Y), rox::core::Max(Z, other.Z),
		        rox::core::Max(W, other.W)};
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::Abs() const noexcept
	{
		ASSERT(IsFinite(), "Vector4 operand must be finite.");
		return {rox::core::Abs(X), rox::core::Abs(Y), rox::core::Abs(Z), rox::core::Abs(W)};
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::Clamped(const Vector4<T>& minimum, const Vector4<T>& maximum) const noexcept
	{
		ASSERT(IsFinite() && minimum.IsFinite() && maximum.IsFinite(), "Vector4 clamp operands must be finite.");
		ASSERT(minimum.X <= maximum.X && minimum.Y <= maximum.Y && minimum.Z <= maximum.Z && minimum.W <= maximum.W, "Vector4 clamp bounds must be ordered per component.");
		return {rox::core::ClampValue(X, minimum.X, maximum.X), rox::core::ClampValue(Y, minimum.Y, maximum.Y),
		        rox::core::ClampValue(Z, minimum.Z, maximum.Z), rox::core::ClampValue(W, minimum.W, maximum.W)};
	}

	template <MathScalar T>
	bool Vector4<T>::IsNearlyZero(T tolerance) const noexcept
	{
		ASSERT(rox::core::IsFinite(tolerance) && tolerance >= T(0.0), "Vector4 comparison tolerance must be finite and nonnegative.");
		return rox::core::Abs(X) <= tolerance && rox::core::Abs(Y) <= tolerance && rox::core::Abs(Z) <= tolerance && rox::core::Abs(W) <= tolerance;
	}

	template <MathScalar T>
	bool Vector4<T>::NearlyEquals(const Vector4<T>& other, T tolerance) const noexcept
	{
		ASSERT(rox::core::IsFinite(tolerance) && tolerance >= T(0.0), "Vector4 comparison tolerance must be finite and nonnegative.");
		return IsFinite() && other.IsFinite() && rox::core::Abs(static_cast<T>(X) - other.X) <= tolerance &&
		       rox::core::Abs(static_cast<T>(Y) - other.Y) <= tolerance &&
		       rox::core::Abs(static_cast<T>(Z) - other.Z) <= tolerance &&
		       rox::core::Abs(static_cast<T>(W) - other.W) <= tolerance;
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::NormalizedOrZero(T epsilon) const noexcept
	{
		ASSERT(IsFinite(), "Vector4 normalization input must be finite.");
		Vector4<T> normalized;
		const bool bNormalized = TryNormalize(&normalized, epsilon);
		return bNormalized ? normalized : Zero();
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::ClampedLength(T maximumLength) const noexcept
	{
		ASSERT(IsFinite() && rox::core::IsFinite(maximumLength) && maximumLength >= T(0), "Length limit and vector must be finite and nonnegative.");
		if (Length() <= maximumLength)
		{
			return *this;
		}

		return Normalized(T(0)) * maximumLength;
	}

	template struct Vector4<float32>;
	template struct Vector4<float64>;
} // namespace rox::math
