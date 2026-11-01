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
	struct Vector2 final
	{
		T X = T(0.0);
		T Y = T(0.0);

		constexpr Vector2() noexcept = default;

		constexpr Vector2(T x, T y) noexcept 
			: X(x), Y(y)
		{
		}

		[[nodiscard]] static constexpr Vector2<T> Zero() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr Vector2<T> One() noexcept
		{
			return {T(1.0), T(1.0)};
		}

		[[nodiscard]] static constexpr Vector2<T> UnitX() noexcept
		{
			return {T(1.0), T(0.0)};
		}

		[[nodiscard]] static constexpr Vector2<T> UnitY() noexcept
		{
			return {T(0.0), T(1.0)};
		}

		[[nodiscard]] constexpr T Dot(const Vector2<T>& other) const noexcept
		{
			return X * other.X + Y * other.Y;
		}

		[[nodiscard]] constexpr T LengthSquared() const noexcept
		{
			return Dot(*this);
		}

		[[nodiscard]] T Length() const noexcept;

		[[nodiscard]] bool IsFinite() const noexcept
		{
			return core::IsFinite(X) && core::IsFinite(Y);
		}

		[[nodiscard]] bool TryNormalize(Vector2<T>* outNormalized, T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] Vector2<T> Normalized(T epsilon = T(1.0e-8)) const noexcept;
		void Normalize(T epsilon = T(1.0e-8)) noexcept;

		[[nodiscard]] T Distance(const Vector2<T>& other) const noexcept;
		[[nodiscard]] T DistanceSquared(const Vector2<T>& other) const noexcept;
		[[nodiscard]] Vector2<T> Min(const Vector2<T>& other) const noexcept;
		[[nodiscard]] Vector2<T> Max(const Vector2<T>& other) const noexcept;
		[[nodiscard]] Vector2<T> Abs() const noexcept;
		[[nodiscard]] Vector2<T> Clamped(const Vector2<T>& minimum, const Vector2<T>& maximum) const noexcept;

		// Comparisons use absolute per-component tolerance; nonfinite components compare false.
		[[nodiscard]] bool IsNearlyZero(T tolerance = T(1.0e-6)) const noexcept;
		[[nodiscard]] bool NearlyEquals(const Vector2<T>& other, T tolerance = T(1.0e-6)) const noexcept;

		// Finite inputs with length at most epsilon return zero.
		[[nodiscard]] Vector2<T> NormalizedOrZero(T epsilon = T(1.0e-8)) const noexcept;

		// Preserves vectors already within the nonnegative length limit.
		[[nodiscard]] Vector2<T> ClampedLength(T maximumLength) const noexcept;

		// Angles are radians; both vectors must be finite and nonzero.
		[[nodiscard]] T Angle(const Vector2<T>& other) const noexcept;
		[[nodiscard]] T SignedAngle(const Vector2<T>& other) const noexcept;

		// Projection requires a finite nonzero basis; its length need not be one.
		[[nodiscard]] Vector2<T> ProjectedOnto(const Vector2<T>& basis) const noexcept;
		[[nodiscard]] Vector2<T> RejectedFrom(const Vector2<T>& basis) const noexcept;

		// Surface normals must have unit length, within 1e-4 squared-length tolerance.
		[[nodiscard]] Vector2<T> Reflected(const Vector2<T>& unitNormal) const noexcept;
		[[nodiscard]] Vector2<T> Slide(const Vector2<T>& unitNormal) const noexcept;
		[[nodiscard]] Vector2<T> MoveTowards(const Vector2<T>& target, T maximumDelta) const noexcept;

		// Alpha is unclamped; interpolation within [0,1] preserves finite endpoints without overflow.
		[[nodiscard]] static constexpr Vector2<T> Lerp(const Vector2<T>& from, const Vector2<T>& to, T alpha) noexcept
		{
			return {core::LerpValue(from.X, to.X, alpha), core::LerpValue(from.Y, to.Y, alpha)};
		}

		[[nodiscard]] constexpr Vector2<T> operator+() const noexcept
		{
			return *this;
		}

		[[nodiscard]] constexpr Vector2<T> operator-() const noexcept
		{
			return {-X, -Y};
		}

		[[nodiscard]] constexpr Vector2<T> operator+(const Vector2<T>& other) const noexcept
		{
			return {X + other.X, Y + other.Y};
		}

		[[nodiscard]] constexpr Vector2<T> operator-(const Vector2<T>& other) const noexcept
		{
			return {X - other.X, Y - other.Y};
		}

		[[nodiscard]] constexpr Vector2<T> operator*(const Vector2<T>& other) const noexcept
		{
			return {X * other.X, Y * other.Y};
		}

		[[nodiscard]] constexpr Vector2<T> operator/(const Vector2<T>& other) const noexcept
		{
			return {X / other.X, Y / other.Y};
		}

		[[nodiscard]] constexpr Vector2<T> operator*(T scalar) const noexcept
		{
			return {X * scalar, Y * scalar};
		}

		[[nodiscard]] constexpr Vector2<T> operator/(T scalar) const noexcept
		{
			return {X / scalar, Y / scalar};
		}

		constexpr Vector2<T>& operator+=(const Vector2<T>& other) noexcept
		{
			X += other.X;
			Y += other.Y;
			return *this;
		}

		constexpr Vector2<T>& operator-=(const Vector2<T>& other) noexcept
		{
			X -= other.X;
			Y -= other.Y;
			return *this;
		}

		constexpr Vector2<T>& operator*=(T scalar) noexcept
		{
			X *= scalar;
			Y *= scalar;
			return *this;
		}

		constexpr Vector2<T>& operator/=(T scalar) noexcept
		{
			X /= scalar;
			Y /= scalar;
			return *this;
		}

		[[nodiscard]] constexpr bool operator==(const Vector2<T>&) const noexcept = default;

		template <MathScalar U>
		explicit constexpr Vector2(const Vector2<U>& other) noexcept
		    : X(static_cast<T>(other.X)), Y(static_cast<T>(other.Y))
		{
		}
	};

	using Vector2f = Vector2<float32>;
	using Vector2d = Vector2<float64>;

	template <MathScalar T>
	[[nodiscard]] constexpr Vector2<T> operator*(T scalar, const Vector2<T>& vector) noexcept
	{
		return vector * scalar;
	}

	static_assert(sizeof(Vector2<float32>) == sizeof(float32) * 2);
	static_assert(sizeof(Vector2<float64>) == sizeof(float64) * 2);
	static_assert(offsetof(Vector2<float32>, Y) == sizeof(float32));
	static_assert(offsetof(Vector2<float64>, Y) == sizeof(float64));
	static_assert(core::IsStandardLayout<Vector2<float32>> && core::IsTriviallyCopyable<Vector2<float32>>);
	static_assert(core::IsStandardLayout<Vector2<float64>> && core::IsTriviallyCopyable<Vector2<float64>>);
} // namespace rox::math
