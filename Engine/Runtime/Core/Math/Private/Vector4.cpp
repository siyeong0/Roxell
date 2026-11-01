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
		const T xyLength = core::Hypot(static_cast<T>(X), static_cast<T>(Y));
		const T zwLength = core::Hypot(static_cast<T>(Z), static_cast<T>(W));
		return static_cast<T>(core::Hypot(xyLength, zwLength));
	}

	template <MathScalar T>
	bool Vector4<T>::TryNormalize(Vector4<T>* outNormalized, T epsilon) const noexcept
	{
		ASSERT(outNormalized != nullptr, "Normalized vector output must not be null.");
		ASSERT(core::IsFinite(epsilon) && epsilon >= T(0), "Normalization epsilon must be finite and nonnegative.");
		if (!IsFinite())
		{
			return false;
		}

		const T scale = core::Max({core::Abs(X), core::Abs(Y), core::Abs(Z), core::Abs(W)});
		if (scale == T(0))
		{
			return false;
		}

		const Vector4<T> scaled{X / scale, Y / scale, Z / scale, W / scale};
		const T length = core::Hypot(core::Hypot(scaled.X, scaled.Y), core::Hypot(scaled.Z, scaled.W));
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
		return core::Hypot(core::Hypot(deltaX, deltaY), core::Hypot(deltaZ, deltaW));
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
		return {core::Min(X, other.X), core::Min(Y, other.Y), core::Min(Z, other.Z),
		        core::Min(W, other.W)};
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::Max(const Vector4<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector4 operands must be finite.");
		return {core::Max(X, other.X), core::Max(Y, other.Y), core::Max(Z, other.Z),
		        core::Max(W, other.W)};
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::Abs() const noexcept
	{
		ASSERT(IsFinite(), "Vector4 operand must be finite.");
		return {core::Abs(X), core::Abs(Y), core::Abs(Z), core::Abs(W)};
	}

	template <MathScalar T>
	Vector4<T> Vector4<T>::Clamped(const Vector4<T>& minimum, const Vector4<T>& maximum) const noexcept
	{
		ASSERT(IsFinite() && minimum.IsFinite() && maximum.IsFinite(), "Vector4 clamp operands must be finite.");
		ASSERT(minimum.X <= maximum.X && minimum.Y <= maximum.Y && minimum.Z <= maximum.Z && minimum.W <= maximum.W, "Vector4 clamp bounds must be ordered per component.");
		return {core::ClampValue(X, minimum.X, maximum.X), core::ClampValue(Y, minimum.Y, maximum.Y),
		        core::ClampValue(Z, minimum.Z, maximum.Z), core::ClampValue(W, minimum.W, maximum.W)};
	}

	template <MathScalar T>
	bool Vector4<T>::IsNearlyZero(T tolerance) const noexcept
	{
		ASSERT(core::IsFinite(tolerance) && tolerance >= T(0.0), "Vector4 comparison tolerance must be finite and nonnegative.");
		return core::Abs(X) <= tolerance && core::Abs(Y) <= tolerance && core::Abs(Z) <= tolerance && core::Abs(W) <= tolerance;
	}

	template <MathScalar T>
	bool Vector4<T>::NearlyEquals(const Vector4<T>& other, T tolerance) const noexcept
	{
		ASSERT(core::IsFinite(tolerance) && tolerance >= T(0.0), "Vector4 comparison tolerance must be finite and nonnegative.");
		return IsFinite() && other.IsFinite() && core::Abs(static_cast<T>(X) - other.X) <= tolerance &&
		       core::Abs(static_cast<T>(Y) - other.Y) <= tolerance &&
		       core::Abs(static_cast<T>(Z) - other.Z) <= tolerance &&
		       core::Abs(static_cast<T>(W) - other.W) <= tolerance;
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
		ASSERT(IsFinite() && core::IsFinite(maximumLength) && maximumLength >= T(0), "Length limit and vector must be finite and nonnegative.");
		if (Length() <= maximumLength)
		{
			return *this;
		}

		return Normalized(T(0)) * maximumLength;
	}

	template struct Vector4<float32>;
	template struct Vector4<float64>;
} // namespace rox::math
