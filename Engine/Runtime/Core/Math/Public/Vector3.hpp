#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <Numeric.hpp>
#include <Comparison.hpp>
#include <TypeTraits.hpp>
#include <Types.h>

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T>
	struct Vector3 final
	{
		T X = T(0.0);
		T Y = T(0.0);
		T Z = T(0.0);

		constexpr Vector3() noexcept = default;

		constexpr Vector3(T x, T y, T z) noexcept 
			: X(x), Y(y), Z(z)
		{
		}

		[[nodiscard]] static constexpr Vector3<T> Zero() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr Vector3<T> One() noexcept
		{
			return {T(1.0), T(1.0), T(1.0)};
		}

		[[nodiscard]] static constexpr Vector3<T> UnitX() noexcept
		{
			return {T(1.0), T(0.0), T(0.0)};
		}

		[[nodiscard]] static constexpr Vector3<T> UnitY() noexcept
		{
			return {T(0.0), T(1.0), T(0.0)};
		}

		[[nodiscard]] static constexpr Vector3<T> UnitZ() noexcept
		{
			return {T(0.0), T(0.0), T(1.0)};
		}

		[[nodiscard]] static constexpr Vector3<T> Right() noexcept
		{
			return UnitX();
		}

		[[nodiscard]] static constexpr Vector3<T> Up() noexcept
		{
			return UnitY();
		}

		[[nodiscard]] static constexpr Vector3<T> Forward() noexcept
		{
			return UnitZ();
		}

		[[nodiscard]] constexpr T Dot(const Vector3<T>& other) const noexcept
		{
			return X * other.X + Y * other.Y + Z * other.Z;
		}

		[[nodiscard]] constexpr Vector3<T> Cross(const Vector3<T>& other) const noexcept
		{
			return {Y * other.Z - Z * other.Y, Z * other.X - X * other.Z, X * other.Y - Y * other.X};
		}

		[[nodiscard]] constexpr T LengthSquared() const noexcept
		{
			return Dot(*this);
		}

		[[nodiscard]] T Length() const noexcept;

		[[nodiscard]] bool IsFinite() const noexcept
		{
			return rox::core::IsFinite(X) && rox::core::IsFinite(Y) && rox::core::IsFinite(Z);
		}

		[[nodiscard]] bool TryNormalize(Vector3<T>* outNormalized, T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] Vector3<T> Normalized(T epsilon = T(1.0e-8)) const noexcept;
		void Normalize(T epsilon = T(1.0e-8)) noexcept;

		[[nodiscard]] T Distance(const Vector3<T>& other) const noexcept;
		[[nodiscard]] T DistanceSquared(const Vector3<T>& other) const noexcept;
		[[nodiscard]] Vector3<T> Min(const Vector3<T>& other) const noexcept;
		[[nodiscard]] Vector3<T> Max(const Vector3<T>& other) const noexcept;
		[[nodiscard]] Vector3<T> Abs() const noexcept;
		[[nodiscard]] Vector3<T> Clamped(const Vector3<T>& minimum, const Vector3<T>& maximum) const noexcept;

		// Comparisons use absolute per-component tolerance; nonfinite components compare false.
		[[nodiscard]] bool IsNearlyZero(T tolerance = T(1.0e-6)) const noexcept;
		[[nodiscard]] bool NearlyEquals(const Vector3<T>& other, T tolerance = T(1.0e-6)) const noexcept;

		// Finite inputs with length at most epsilon return zero.
		[[nodiscard]] Vector3<T> NormalizedOrZero(T epsilon = T(1.0e-8)) const noexcept;

		// Preserves vectors already within the nonnegative length limit.
		[[nodiscard]] Vector3<T> ClampedLength(T maximumLength) const noexcept;

		// Angles are radians; both vectors must be finite and nonzero.
		[[nodiscard]] T Angle(const Vector3<T>& other) const noexcept;
		[[nodiscard]] T SignedAngle(const Vector3<T>& other, const Vector3<T>& unitAxis) const noexcept;

		// Projection requires a finite nonzero basis; its length need not be one.
		[[nodiscard]] Vector3<T> ProjectedOnto(const Vector3<T>& basis) const noexcept;
		[[nodiscard]] Vector3<T> RejectedFrom(const Vector3<T>& basis) const noexcept;

		// Surface normals must have unit length, within 1e-4 squared-length tolerance.
		[[nodiscard]] Vector3<T> Reflected(const Vector3<T>& unitNormal) const noexcept;
		[[nodiscard]] Vector3<T> Slide(const Vector3<T>& unitNormal) const noexcept;
		[[nodiscard]] Vector3<T> MoveTowards(const Vector3<T>& target, T maximumDelta) const noexcept;

		// Returns a unit perpendicular using the least-aligned axis; input must be finite and nonzero.
		[[nodiscard]] Vector3<T> Perpendicular() const noexcept;

		// The unit normal opposes this unit incident direction; ratio is incident/transmitted index.
		// Total internal reflection returns false and writes zero; output may alias an input.
		[[nodiscard]] bool Refracted(const Vector3<T>& unitNormal, T indexRatio,
		                             Vector3<T>* outDirection) const noexcept;

		// Alpha is unclamped; interpolation within [0,1] preserves finite endpoints without overflow.
		[[nodiscard]] static constexpr Vector3<T> Lerp(const Vector3<T>& from, const Vector3<T>& to, T alpha) noexcept
		{
			return {rox::core::LerpValue(from.X, to.X, alpha), 
					rox::core::LerpValue(from.Y, to.Y, alpha),
			        rox::core::LerpValue(from.Z, to.Z, alpha)};
		}

		[[nodiscard]] constexpr Vector3<T> operator+() const noexcept
		{
			return *this;
		}

		[[nodiscard]] constexpr Vector3<T> operator-() const noexcept
		{
			return {-X, -Y, -Z};
		}

		[[nodiscard]] constexpr Vector3<T> operator+(const Vector3<T>& other) const noexcept
		{
			return {X + other.X, Y + other.Y, Z + other.Z};
		}

		[[nodiscard]] constexpr Vector3<T> operator-(const Vector3<T>& other) const noexcept
		{
			return {X - other.X, Y - other.Y, Z - other.Z};
		}

		[[nodiscard]] constexpr Vector3<T> operator*(const Vector3<T>& other) const noexcept
		{
			return {X * other.X, Y * other.Y, Z * other.Z};
		}

		[[nodiscard]] constexpr Vector3<T> operator/(const Vector3<T>& other) const noexcept
		{
			return {X / other.X, Y / other.Y, Z / other.Z};
		}

		[[nodiscard]] constexpr Vector3<T> operator*(T scalar) const noexcept
		{
			return {X * scalar, Y * scalar, Z * scalar};
		}

		[[nodiscard]] constexpr Vector3<T> operator/(T scalar) const noexcept
		{
			return {X / scalar, Y / scalar, Z / scalar};
		}

		constexpr Vector3<T>& operator+=(const Vector3<T>& other) noexcept
		{
			X += other.X;
			Y += other.Y;
			Z += other.Z;
			return *this;
		}

		constexpr Vector3<T>& operator-=(const Vector3<T>& other) noexcept
		{
			X -= other.X;
			Y -= other.Y;
			Z -= other.Z;
			return *this;
		}

		constexpr Vector3<T>& operator*=(T scalar) noexcept
		{
			X *= scalar;
			Y *= scalar;
			Z *= scalar;
			return *this;
		}

		constexpr Vector3<T>& operator/=(T scalar) noexcept
		{
			X /= scalar;
			Y /= scalar;
			Z /= scalar;
			return *this;
		}

		[[nodiscard]] constexpr bool operator==(const Vector3<T>&) const noexcept = default;

		template <MathScalar U>
		explicit constexpr Vector3(const Vector3<U>& other) noexcept
		    : X(static_cast<T>(other.X)), Y(static_cast<T>(other.Y)), Z(static_cast<T>(other.Z))
		{
		}
	};

	using Vector3f = Vector3<float32>;
	using Vector3d = Vector3<float64>;

	template <MathScalar T>
	[[nodiscard]] constexpr Vector3<T> operator*(T scalar, const Vector3<T>& vector) noexcept
	{
		return vector * scalar;
	}

	static_assert(sizeof(Vector3<float32>) == sizeof(float32) * 3);
	static_assert(sizeof(Vector3<float64>) == sizeof(float64) * 3);
	static_assert(offsetof(Vector3<float32>, Z) == sizeof(float32) * 2);
	static_assert(offsetof(Vector3<float64>, Z) == sizeof(float64) * 2);
	static_assert(rox::core::IsStandardLayout<Vector3<float32>> && rox::core::IsTriviallyCopyable<Vector3<float32>>);
	static_assert(rox::core::IsStandardLayout<Vector3<float64>> && rox::core::IsTriviallyCopyable<Vector3<float64>>);
} // namespace rox::math
