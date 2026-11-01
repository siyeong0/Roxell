#pragma once

#include <MathScalar.hpp>

#include <Hash.h>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Vector2.hpp>

#include <cstddef>
#include <type_traits>

namespace rox::math
{
	struct Int2 final
	{
		int32 X = 0;
		int32 Y = 0;

		constexpr Int2() noexcept = default;

		constexpr Int2(const int32 x, const int32 y) noexcept : X(x), Y(y)
		{
		}

		[[nodiscard]] static constexpr Int2 Zero() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr Int2 One() noexcept
		{
			return {1, 1};
		}

		[[nodiscard]] static constexpr Int2 UnitX() noexcept
		{
			return {1, 0};
		}

		[[nodiscard]] static constexpr Int2 UnitY() noexcept
		{
			return {0, 1};
		}

		[[nodiscard]] constexpr Int2 operator+() const noexcept
		{
			return *this;
		}

		[[nodiscard]] constexpr bool operator==(const Int2&) const noexcept = default;

		// Arithmetic requires an int32 result in every lane; divisors must be nonzero.
		[[nodiscard]] Int2 operator-() const noexcept;
		[[nodiscard]] Int2 operator+(const Int2& other) const noexcept;
		[[nodiscard]] Int2 operator-(const Int2& other) const noexcept;
		[[nodiscard]] Int2 operator*(const Int2& other) const noexcept;
		[[nodiscard]] Int2 operator/(const Int2& other) const noexcept;
		[[nodiscard]] Int2 operator*(int32 scalar) const noexcept;
		[[nodiscard]] Int2 operator/(int32 scalar) const noexcept;
		Int2& operator+=(const Int2& other) noexcept;
		Int2& operator-=(const Int2& other) noexcept;
		Int2& operator*=(const Int2& other) noexcept;
		Int2& operator/=(const Int2& other) noexcept;
		Int2& operator*=(int32 scalar) noexcept;
		Int2& operator/=(int32 scalar) noexcept;
		[[nodiscard]] Int2 Min(const Int2& other) const noexcept;
		[[nodiscard]] Int2 Max(const Int2& other) const noexcept;
		[[nodiscard]] Int2 Abs() const noexcept;
		[[nodiscard]] Int2 Clamped(const Int2& minimum, const Int2& maximum) const noexcept;

		// Float conversions require finite inputs and representable rounded int32 lanes. Ties round away from zero.
		template <MathScalar T>
		[[nodiscard]] static Int2 Floor(const Vector2<T>& value) noexcept;

		template <MathScalar T>
		[[nodiscard]] static Int2 Ceil(const Vector2<T>& value) noexcept;

		template <MathScalar T>
		[[nodiscard]] static Int2 Round(const Vector2<T>& value) noexcept;

		// Large integer magnitudes may lose precision in float32.
		template <MathScalar T = float32>
		[[nodiscard]] Vector2<T> ToVector2() const noexcept;
	};

	[[nodiscard]] Int2 operator*(int32 scalar, const Int2& value) noexcept;

	static_assert(sizeof(Int2) == 2 * sizeof(int32) && alignof(Int2) == alignof(int32));
	static_assert(offsetof(Int2, X) == 0 && offsetof(Int2, Y) == sizeof(int32));
	static_assert(core::IsStandardLayout<Int2> && core::IsTriviallyCopyable<Int2>);
} // namespace rox::math

namespace rox::core
{
	template <>
	struct DefaultHash<rox::math::Int2>
	{
		[[nodiscard]] uint64 operator()(const rox::math::Int2& value) const noexcept
		{
			const uint64 seed = HashCombine(0, DefaultHash<int32>{}(value.X));
			return HashCombine(seed, DefaultHash<int32>{}(value.Y));
		}
	};

} // namespace rox::core
