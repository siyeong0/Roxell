#include <Vector2.hpp>
#include <Numeric.hpp>
#include <Comparison.hpp>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	template <MathScalar T>
	T Vector2<T>::Length() const noexcept
	{
		return static_cast<T>(core::Hypot(static_cast<T>(X), static_cast<T>(Y)));
	}

	template <MathScalar T>
	bool Vector2<T>::TryNormalize(Vector2<T>* outNormalized, T epsilon) const noexcept
	{
		ASSERT(outNormalized != nullptr, "Normalized vector output must not be null.");
		ASSERT(core::IsFinite(epsilon) && epsilon >= T(0), "Normalization epsilon must be finite and nonnegative.");
		if (!IsFinite())
		{
			return false;
		}

		const T scale = core::Max({core::Abs(X), core::Abs(Y)});
		if (scale == T(0))
		{
			return false;
		}

		const Vector2<T> scaled{X / scale, Y / scale};
		const T length = core::Hypot(scaled.X, scaled.Y);
		if (scale <= epsilon / length)
		{
			return false;
		}

		*outNormalized = scaled / length;
		return true;
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::Normalized(T epsilon) const noexcept
	{
		Vector2<T> result;
		const bool bNormalized = TryNormalize(&result, epsilon);
		ASSERT(bNormalized, "Vector2 must be finite and longer than the normalization epsilon.");
		return result;
	}

	template <MathScalar T>
	void Vector2<T>::Normalize(T epsilon) noexcept
	{
		*this = Normalized(epsilon);
	}

	template <MathScalar T>
	T Vector2<T>::Distance(const Vector2<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector operands must be finite.");
		const T deltaX = X - other.X;
		const T deltaY = Y - other.Y;
		return core::Hypot(deltaX, deltaY);
	}

	template <MathScalar T>
	T Vector2<T>::DistanceSquared(const Vector2<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector2 operands must be finite.");
		const T deltaX = static_cast<T>(X) - other.X;
		const T deltaY = static_cast<T>(Y) - other.Y;
		return static_cast<T>(deltaX * deltaX + deltaY * deltaY);
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::Min(const Vector2<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector2 operands must be finite.");
		return {core::Min(X, other.X), core::Min(Y, other.Y)};
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::Max(const Vector2<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector2 operands must be finite.");
		return {core::Max(X, other.X), core::Max(Y, other.Y)};
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::Abs() const noexcept
	{
		ASSERT(IsFinite(), "Vector2 operand must be finite.");
		return {core::Abs(X), core::Abs(Y)};
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::Clamped(const Vector2<T>& minimum, const Vector2<T>& maximum) const noexcept
	{
		ASSERT(IsFinite() && minimum.IsFinite() && maximum.IsFinite(), "Vector2 clamp operands must be finite.");
		ASSERT(minimum.X <= maximum.X && minimum.Y <= maximum.Y, "Vector2 clamp bounds must be ordered per component.");
		return {core::ClampValue(X, minimum.X, maximum.X), core::ClampValue(Y, minimum.Y, maximum.Y)};
	}

	template <MathScalar T>
	bool Vector2<T>::IsNearlyZero(T tolerance) const noexcept
	{
		ASSERT(core::IsFinite(tolerance) && tolerance >= T(0.0), "Vector2 comparison tolerance must be finite and nonnegative.");
		return core::Abs(X) <= tolerance && core::Abs(Y) <= tolerance;
	}

	template <MathScalar T>
	bool Vector2<T>::NearlyEquals(const Vector2<T>& other, T tolerance) const noexcept
	{
		ASSERT(core::IsFinite(tolerance) && tolerance >= T(0.0), "Vector2 comparison tolerance must be finite and nonnegative.");
		return IsFinite() && other.IsFinite() && core::Abs(static_cast<T>(X) - other.X) <= tolerance &&
		       core::Abs(static_cast<T>(Y) - other.Y) <= tolerance;
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::NormalizedOrZero(T epsilon) const noexcept
	{
		ASSERT(IsFinite(), "Vector2 normalization input must be finite.");
		Vector2<T> normalized;
		const bool bNormalized = TryNormalize(&normalized, epsilon);
		return bNormalized ? normalized : Zero();
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::ClampedLength(T maximumLength) const noexcept
	{
		ASSERT(IsFinite() && core::IsFinite(maximumLength) && maximumLength >= T(0), "Length limit and vector must be finite and nonnegative.");
		if (Length() <= maximumLength)
		{
			return *this;
		}

		return Normalized(T(0)) * maximumLength;
	}

	template <MathScalar T>
	T Vector2<T>::Angle(const Vector2<T>& other) const noexcept
	{
		const Vector2<T> a = Normalized(T(0));
		const Vector2<T> b = other.Normalized(T(0));
		return core::Atan2(core::Abs(a.X * b.Y - a.Y * b.X), a.Dot(b));
	}

	template <MathScalar T>
	T Vector2<T>::SignedAngle(const Vector2<T>& other) const noexcept
	{
		const T angle = Angle(other);
		const Vector2<T> a = Normalized(T(0));
		const Vector2<T> b = other.Normalized(T(0));
		const T orientation = a.X * b.Y - a.Y * b.X;
		return orientation < T(0.0) ? -angle : angle;
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::ProjectedOnto(const Vector2<T>& basis) const noexcept
	{
		ASSERT(IsFinite(), "Projection input must be finite.");
		const Vector2<T> unit = basis.Normalized(T(0));
		const T scale = core::Max({core::Abs(X), core::Abs(Y)});
		if (scale == T(0))
		{
			return Zero();
		}

		const Vector2<T> scaled = *this / scale;
		return (unit * scaled.Dot(unit)) * scale;
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::RejectedFrom(const Vector2<T>& basis) const noexcept
	{
		ASSERT(IsFinite(), "Projection input must be finite.");
		const Vector2<T> unit = basis.Normalized(T(0));
		const T scale = core::Max({core::Abs(X), core::Abs(Y)});
		if (scale == T(0))
		{
			return Zero();
		}

		const Vector2<T> scaled = *this / scale;
		return (scaled - unit * scaled.Dot(unit)) * scale;
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::Reflected(const Vector2<T>& unitNormal) const noexcept
	{
		ASSERT(IsFinite(), "Vector2 reflection input must be finite.");
		ASSERT(unitNormal.IsFinite() && core::Abs(unitNormal.LengthSquared() - T(1.0)) <= T(1.0e-4), "Reflection normal must have unit length.");
		const T dot = static_cast<T>(X) * unitNormal.X + static_cast<T>(Y) * unitNormal.Y;
		return {static_cast<T>(X - T(2.0) * dot * unitNormal.X), static_cast<T>(Y - T(2.0) * dot * unitNormal.Y)};
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::Slide(const Vector2<T>& unitNormal) const noexcept
	{
		ASSERT(IsFinite(), "Vector2 slide input must be finite.");
		ASSERT(unitNormal.IsFinite() && core::Abs(unitNormal.LengthSquared() - T(1.0)) <= T(1.0e-4), "Slide normal must have unit length.");
		const T dot = static_cast<T>(X) * unitNormal.X + static_cast<T>(Y) * unitNormal.Y;
		return {static_cast<T>(X - dot * unitNormal.X), static_cast<T>(Y - dot * unitNormal.Y)};
	}

	template <MathScalar T>
	Vector2<T> Vector2<T>::MoveTowards(const Vector2<T>& target, T maximumDelta) const noexcept
	{
		ASSERT(IsFinite() && target.IsFinite(), "Vector2 movement operands must be finite.");
		ASSERT(core::IsFinite(maximumDelta) && maximumDelta >= T(0.0), "Maximum movement must be finite and nonnegative.");
		const T deltaX = static_cast<T>(target.X) - X;
		const T deltaY = static_cast<T>(target.Y) - Y;
		const T distance = core::Hypot(deltaX, deltaY);

		if (distance <= maximumDelta)
		{
			return target;
		}

		const T scale = static_cast<T>(maximumDelta) / distance;
		return {static_cast<T>(X + deltaX * scale), static_cast<T>(Y + deltaY * scale)};
	}

	template struct Vector2<float32>;
	template struct Vector2<float64>;
} // namespace rox::math
