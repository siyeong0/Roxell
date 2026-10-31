#include <Vector3.hpp>
#include <Numeric.hpp>
#include <Comparison.hpp>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	template <MathScalar T>
	T Vector3<T>::Length() const noexcept
	{
		return static_cast<T>(rox::core::Hypot(static_cast<T>(X), static_cast<T>(Y), static_cast<T>(Z)));
	}

	template <MathScalar T>
	bool Vector3<T>::TryNormalize(Vector3<T>* outNormalized, T epsilon) const noexcept
	{
		ASSERT(outNormalized != nullptr, "Normalized vector output must not be null.");
		ASSERT(rox::core::IsFinite(epsilon) && epsilon >= T(0), "Normalization epsilon must be finite and nonnegative.");
		if (!IsFinite())
		{
			return false;
		}

		const T scale = rox::core::Max({rox::core::Abs(X), rox::core::Abs(Y), rox::core::Abs(Z)});
		if (scale == T(0))
		{
			return false;
		}

		const Vector3<T> scaled{X / scale, Y / scale, Z / scale};
		const T length = rox::core::Hypot(scaled.X, scaled.Y, scaled.Z);
		if (scale <= epsilon / length)
		{
			return false;
		}

		*outNormalized = scaled / length;
		return true;
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::Normalized(T epsilon) const noexcept
	{
		Vector3<T> result;
		const bool bNormalized = TryNormalize(&result, epsilon);
		ASSERT(bNormalized, "Vector3 must be finite and longer than the normalization epsilon.");
		return result;
	}

	template <MathScalar T>
	void Vector3<T>::Normalize(T epsilon) noexcept
	{
		*this = Normalized(epsilon);
	}

	template <MathScalar T>
	T Vector3<T>::Distance(const Vector3<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector operands must be finite.");
		const T deltaX = X - other.X;
		const T deltaY = Y - other.Y;
		const T deltaZ = Z - other.Z;
		return rox::core::Hypot(deltaX, deltaY, deltaZ);
	}

	template <MathScalar T>
	T Vector3<T>::DistanceSquared(const Vector3<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector3 operands must be finite.");
		const T deltaX = static_cast<T>(X) - other.X;
		const T deltaY = static_cast<T>(Y) - other.Y;
		const T deltaZ = static_cast<T>(Z) - other.Z;
		return static_cast<T>(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::Min(const Vector3<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector3 operands must be finite.");
		return {rox::core::Min(X, other.X), rox::core::Min(Y, other.Y), rox::core::Min(Z, other.Z)};
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::Max(const Vector3<T>& other) const noexcept
	{
		ASSERT(IsFinite() && other.IsFinite(), "Vector3 operands must be finite.");
		return {rox::core::Max(X, other.X), rox::core::Max(Y, other.Y), rox::core::Max(Z, other.Z)};
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::Abs() const noexcept
	{
		ASSERT(IsFinite(), "Vector3 operand must be finite.");
		return {rox::core::Abs(X), rox::core::Abs(Y), rox::core::Abs(Z)};
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::Clamped(const Vector3<T>& minimum, const Vector3<T>& maximum) const noexcept
	{
		ASSERT(IsFinite() && minimum.IsFinite() && maximum.IsFinite(), "Vector3 clamp operands must be finite.");
		ASSERT(minimum.X <= maximum.X && minimum.Y <= maximum.Y && minimum.Z <= maximum.Z, "Vector3 clamp bounds must be ordered per component.");
		return {rox::core::ClampValue(X, minimum.X, maximum.X), rox::core::ClampValue(Y, minimum.Y, maximum.Y),
		        rox::core::ClampValue(Z, minimum.Z, maximum.Z)};
	}

	template <MathScalar T>
	bool Vector3<T>::IsNearlyZero(T tolerance) const noexcept
	{
		ASSERT(rox::core::IsFinite(tolerance) && tolerance >= T(0.0), "Vector3 comparison tolerance must be finite and nonnegative.");
		return rox::core::Abs(X) <= tolerance && rox::core::Abs(Y) <= tolerance && rox::core::Abs(Z) <= tolerance;
	}

	template <MathScalar T>
	bool Vector3<T>::NearlyEquals(const Vector3<T>& other, T tolerance) const noexcept
	{
		ASSERT(rox::core::IsFinite(tolerance) && tolerance >= T(0.0), "Vector3 comparison tolerance must be finite and nonnegative.");
		return IsFinite() && other.IsFinite() && rox::core::Abs(static_cast<T>(X) - other.X) <= tolerance &&
		       rox::core::Abs(static_cast<T>(Y) - other.Y) <= tolerance &&
		       rox::core::Abs(static_cast<T>(Z) - other.Z) <= tolerance;
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::NormalizedOrZero(T epsilon) const noexcept
	{
		ASSERT(IsFinite(), "Vector3 normalization input must be finite.");
		Vector3<T> normalized;
		const bool bNormalized = TryNormalize(&normalized, epsilon);
		return bNormalized ? normalized : Zero();
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::ClampedLength(T maximumLength) const noexcept
	{
		ASSERT(IsFinite() && rox::core::IsFinite(maximumLength) && maximumLength >= T(0), "Length limit and vector must be finite and nonnegative.");
		if (Length() <= maximumLength)
		{
			return *this;
		}

		return Normalized(T(0)) * maximumLength;
	}

	template <MathScalar T>
	T Vector3<T>::Angle(const Vector3<T>& other) const noexcept
	{
		const Vector3<T> a = Normalized(T(0));
		const Vector3<T> b = other.Normalized(T(0));
		return rox::core::Atan2(a.Cross(b).Length(), a.Dot(b));
	}

	template <MathScalar T>
	T Vector3<T>::SignedAngle(const Vector3<T>& other, const Vector3<T>& unitAxis) const noexcept
	{
		ASSERT(unitAxis.IsFinite() && rox::core::Abs(unitAxis.LengthSquared() - T(1.0)) <= T(1.0e-4), "Signed angle axis must have unit length.");
		const T angle = Angle(other);
		const Vector3<T> a = Normalized(T(0));
		const Vector3<T> b = other.Normalized(T(0));
		const T orientation = a.Cross(b).Dot(unitAxis);
		return orientation < T(0.0) ? -angle : angle;
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::ProjectedOnto(const Vector3<T>& basis) const noexcept
	{
		ASSERT(IsFinite(), "Projection input must be finite.");
		const Vector3<T> unit = basis.Normalized(T(0));
		const T scale = rox::core::Max({rox::core::Abs(X), rox::core::Abs(Y), rox::core::Abs(Z)});
		if (scale == T(0))
		{
			return Zero();
		}

		const Vector3<T> scaled = *this / scale;
		return (unit * scaled.Dot(unit)) * scale;
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::RejectedFrom(const Vector3<T>& basis) const noexcept
	{
		ASSERT(IsFinite(), "Projection input must be finite.");
		const Vector3<T> unit = basis.Normalized(T(0));
		const T scale = rox::core::Max({rox::core::Abs(X), rox::core::Abs(Y), rox::core::Abs(Z)});
		if (scale == T(0))
		{
			return Zero();
		}

		const Vector3<T> scaled = *this / scale;
		return (scaled - unit * scaled.Dot(unit)) * scale;
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::Reflected(const Vector3<T>& unitNormal) const noexcept
	{
		ASSERT(IsFinite(), "Vector3 reflection input must be finite.");
		ASSERT(unitNormal.IsFinite() && rox::core::Abs(unitNormal.LengthSquared() - T(1.0)) <= T(1.0e-4), "Reflection normal must have unit length.");
		const T dot = static_cast<T>(X) * unitNormal.X + static_cast<T>(Y) * unitNormal.Y + static_cast<T>(Z) * unitNormal.Z;
		return {static_cast<T>(X - T(2.0) * dot * unitNormal.X), 
				static_cast<T>(Y - T(2.0) * dot * unitNormal.Y),
		        static_cast<T>(Z - T(2.0) * dot * unitNormal.Z)};
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::Slide(const Vector3<T>& unitNormal) const noexcept
	{
		ASSERT(IsFinite(), "Vector3 slide input must be finite.");
		ASSERT(unitNormal.IsFinite() && rox::core::Abs(unitNormal.LengthSquared() - T(1.0)) <= T(1.0e-4), "Slide normal must have unit length.");
		const T dot = static_cast<T>(X) * unitNormal.X + static_cast<T>(Y) * unitNormal.Y + static_cast<T>(Z) * unitNormal.Z;
		return {static_cast<T>(X - dot * unitNormal.X),
				static_cast<T>(Y - dot * unitNormal.Y),
		        static_cast<T>(Z - dot * unitNormal.Z)};
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::MoveTowards(const Vector3<T>& target, T maximumDelta) const noexcept
	{
		ASSERT(IsFinite() && target.IsFinite(), "Vector3 movement operands must be finite.");
		ASSERT(rox::core::IsFinite(maximumDelta) && maximumDelta >= T(0.0), "Maximum movement must be finite and nonnegative.");
		const T deltaX = static_cast<T>(target.X) - X;
		const T deltaY = static_cast<T>(target.Y) - Y;
		const T deltaZ = static_cast<T>(target.Z) - Z;
		const T distance = rox::core::Hypot(deltaX, deltaY, deltaZ);

		if (distance <= maximumDelta)
		{
			return target;
		}

		const T scale = static_cast<T>(maximumDelta) / distance;
		return {static_cast<T>(X + deltaX * scale), 
				static_cast<T>(Y + deltaY * scale),
		        static_cast<T>(Z + deltaZ * scale)};
	}

	template <MathScalar T>
	Vector3<T> Vector3<T>::Perpendicular() const noexcept
	{
		ASSERT(IsFinite() && (X != T(0.0) || Y != T(0.0) || Z != T(0.0)), "Perpendicular input must be finite and nonzero.");
		const Vector3<T> absolute = Abs();
		const Vector3<T> basis = absolute.X <= absolute.Y && absolute.X <= absolute.Z ? UnitX()
		                         : absolute.Y <= absolute.Z                           ? UnitY()
		                                                                              : UnitZ();
		return Cross(basis).Normalized(T(0.0));
	}

	template <MathScalar T>
	bool Vector3<T>::Refracted(const Vector3<T>& unitNormal, T indexRatio, Vector3<T>* outDirection) const noexcept
	{
		ASSERT(outDirection != nullptr, "Refraction output must not be null.");
		ASSERT(IsFinite() && rox::core::Abs(LengthSquared() - T(1.0)) <= T(1.0e-4), "Incident direction must have unit length.");
		ASSERT(unitNormal.IsFinite() && rox::core::Abs(unitNormal.LengthSquared() - T(1.0)) <= T(1.0e-4), "Refraction normal must have unit length.");
		ASSERT(rox::core::IsFinite(indexRatio) && indexRatio > T(0.0), "Refraction index ratio must be positive and finite.");
		const T incidentDot = static_cast<T>(X) * unitNormal.X + static_cast<T>(Y) * unitNormal.Y + static_cast<T>(Z) * unitNormal.Z;
		ASSERT(incidentDot <= T(1.0e-4), "Refraction normal must oppose the incident direction.");
		const T normalSquared = static_cast<T>(unitNormal.X) * unitNormal.X +
		                        static_cast<T>(unitNormal.Y) * unitNormal.Y +
		                        static_cast<T>(unitNormal.Z) * unitNormal.Z;
		const T normalLength = rox::core::Sqrt(normalSquared);
		const T incidentLength = rox::core::Sqrt(static_cast<T>(X) * X + static_cast<T>(Y) * Y + static_cast<T>(Z) * Z);
		const T crossX = static_cast<T>(Y) * unitNormal.Z - static_cast<T>(Z) * unitNormal.Y;
		const T crossY = static_cast<T>(Z) * unitNormal.X - static_cast<T>(X) * unitNormal.Z;
		const T crossZ = static_cast<T>(X) * unitNormal.Y - static_cast<T>(Y) * unitNormal.X;
		const T tangentDivisor = incidentLength * normalSquared;
		const T ratio = indexRatio;
		// Cross products preserve zero tangent for exactly parallel inputs despite unit-length rounding.
		const T tangentX = ratio * (unitNormal.Y * crossZ - unitNormal.Z * crossY) / tangentDivisor;
		const T tangentY = ratio * (unitNormal.Z * crossX - unitNormal.X * crossZ) / tangentDivisor;
		const T tangentZ = ratio * (unitNormal.X * crossY - unitNormal.Y * crossX) / tangentDivisor;
		const T discriminant = T(1.0) - (tangentX * tangentX + tangentY * tangentY + tangentZ * tangentZ);

		if (discriminant < T(0.0))
		{
			*outDirection = Zero();
			return false;
		}

		const T normalScale = rox::core::Sqrt(discriminant) / normalLength;
		const Vector3<T> refracted{static_cast<T>(tangentX - normalScale * unitNormal.X),
		                           static_cast<T>(tangentY - normalScale * unitNormal.Y),
		                           static_cast<T>(tangentZ - normalScale * unitNormal.Z)};
		*outDirection = refracted;
		return true;
	}

	template struct Vector3<float32>;
	template struct Vector3<float64>;
} // namespace rox::math
