#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <Numeric.hpp>
#include <Comparison.hpp>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Vector3.hpp>

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T>
	struct Vector4 final
	{
		T X = T(0.0);
		T Y = T(0.0);
		T Z = T(0.0);
		T W = T(0.0);

		constexpr Vector4() noexcept = default;

		constexpr Vector4(T x, T y, T z, T w) noexcept 
			: X(x), Y(y), Z(z), W(w)
		{
		}

		constexpr Vector4(const Vector3<T>& xyz, T w) noexcept 
			: X(xyz.X), Y(xyz.Y), Z(xyz.Z), W(w)
		{
		}

		[[nodiscard]] static constexpr Vector4<T> Zero() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr Vector4<T> One() noexcept
		{
			return {T(1.0), T(1.0), T(1.0), T(1.0)};
		}

		[[nodiscard]] constexpr Vector3<T> XYZ() const noexcept
		{
			return {X, Y, Z};
		}

		[[nodiscard]] constexpr T Dot(const Vector4<T>& other) const noexcept
		{
			return X * other.X + Y * other.Y + Z * other.Z + W * other.W;
		}

		[[nodiscard]] constexpr T LengthSquared() const noexcept
		{
			return Dot(*this);
		}

		[[nodiscard]] T Length() const noexcept;

		[[nodiscard]] bool IsFinite() const noexcept
		{
			return rox::core::IsFinite(X) && rox::core::IsFinite(Y) && rox::core::IsFinite(Z) && rox::core::IsFinite(W);
		}

		[[nodiscard]] bool TryNormalize(Vector4<T>* outNormalized, T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] Vector4<T> Normalized(T epsilon = T(1.0e-8)) const noexcept;
		void Normalize(T epsilon = T(1.0e-8)) noexcept;

		[[nodiscard]] T Distance(const Vector4<T>& other) const noexcept;
		[[nodiscard]] T DistanceSquared(const Vector4<T>& other) const noexcept;
		[[nodiscard]] Vector4<T> Min(const Vector4<T>& other) const noexcept;
		[[nodiscard]] Vector4<T> Max(const Vector4<T>& other) const noexcept;
		[[nodiscard]] Vector4<T> Abs() const noexcept;
		[[nodiscard]] Vector4<T> Clamped(const Vector4<T>& minimum, const Vector4<T>& maximum) const noexcept;

		// Comparisons use absolute per-component tolerance; nonfinite components compare false.
		[[nodiscard]] bool IsNearlyZero(T tolerance = T(1.0e-6)) const noexcept;
		[[nodiscard]] bool NearlyEquals(const Vector4<T>& other, T tolerance = T(1.0e-6)) const noexcept;

		// Finite inputs with length at most epsilon return zero.
		[[nodiscard]] Vector4<T> NormalizedOrZero(T epsilon = T(1.0e-8)) const noexcept;

		// Preserves vectors already within the nonnegative length limit.
		[[nodiscard]] Vector4<T> ClampedLength(T maximumLength) const noexcept;

		// Alpha is unclamped; interpolation within [0,1] preserves finite endpoints without overflow.
		[[nodiscard]] static constexpr Vector4<T> Lerp(const Vector4<T>& from, const Vector4<T>& to, T alpha) noexcept
		{
			return {rox::core::LerpValue(from.X, to.X, alpha), 
					rox::core::LerpValue(from.Y, to.Y, alpha),
			        rox::core::LerpValue(from.Z, to.Z, alpha), 
					rox::core::LerpValue(from.W, to.W, alpha)};
		}

		[[nodiscard]] constexpr Vector4<T> operator+() const noexcept
		{
			return *this;
		}

		[[nodiscard]] constexpr Vector4<T> operator-() const noexcept
		{
			return {-X, -Y, -Z, -W};
		}

		[[nodiscard]] constexpr Vector4<T> operator+(const Vector4<T>& other) const noexcept
		{
			return {X + other.X, Y + other.Y, Z + other.Z, W + other.W};
		}

		[[nodiscard]] constexpr Vector4<T> operator-(const Vector4<T>& other) const noexcept
		{
			return {X - other.X, Y - other.Y, Z - other.Z, W - other.W};
		}

		[[nodiscard]] constexpr Vector4<T> operator*(const Vector4<T>& other) const noexcept
		{
			return {X * other.X, Y * other.Y, Z * other.Z, W * other.W};
		}

		[[nodiscard]] constexpr Vector4<T> operator/(const Vector4<T>& other) const noexcept
		{
			return {X / other.X, Y / other.Y, Z / other.Z, W / other.W};
		}

		[[nodiscard]] constexpr Vector4<T> operator*(T scalar) const noexcept
		{
			return {X * scalar, Y * scalar, Z * scalar, W * scalar};
		}

		[[nodiscard]] constexpr Vector4<T> operator/(T scalar) const noexcept
		{
			return {X / scalar, Y / scalar, Z / scalar, W / scalar};
		}

		constexpr Vector4<T>& operator+=(const Vector4<T>& other) noexcept
		{
			X += other.X;
			Y += other.Y;
			Z += other.Z;
			W += other.W;
			return *this;
		}

		constexpr Vector4<T>& operator-=(const Vector4<T>& other) noexcept
		{
			X -= other.X;
			Y -= other.Y;
			Z -= other.Z;
			W -= other.W;
			return *this;
		}

		constexpr Vector4<T>& operator*=(T scalar) noexcept
		{
			X *= scalar;
			Y *= scalar;
			Z *= scalar;
			W *= scalar;
			return *this;
		}

		constexpr Vector4<T>& operator/=(T scalar) noexcept
		{
			X /= scalar;
			Y /= scalar;
			Z /= scalar;
			W /= scalar;
			return *this;
		}

		[[nodiscard]] constexpr bool operator==(const Vector4<T>&) const noexcept = default;

		template <MathScalar U>
		explicit constexpr Vector4(const Vector4<U>& other) noexcept
		    : X(static_cast<T>(other.X))
			, Y(static_cast<T>(other.Y))
			, Z(static_cast<T>(other.Z))
		    , W(static_cast<T>(other.W))
		{
		}
	};

	using Vector4f = Vector4<float32>;
	using Vector4d = Vector4<float64>;

	template <MathScalar T>
	[[nodiscard]] constexpr Vector4<T> operator*(T scalar, const Vector4<T>& vector) noexcept
	{
		return vector * scalar;
	}

	static_assert(sizeof(Vector4<float32>) == sizeof(float32) * 4);
	static_assert(sizeof(Vector4<float64>) == sizeof(float64) * 4);
	static_assert(offsetof(Vector4<float32>, W) == sizeof(float32) * 3);
	static_assert(offsetof(Vector4<float64>, W) == sizeof(float64) * 3);
	static_assert(rox::core::IsStandardLayout<Vector4<float32>> && rox::core::IsTriviallyCopyable<Vector4<float32>>);
	static_assert(rox::core::IsStandardLayout<Vector4<float64>> && rox::core::IsTriviallyCopyable<Vector4<float64>>);
} // namespace rox::math
