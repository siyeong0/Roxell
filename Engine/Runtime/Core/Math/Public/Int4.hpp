#pragma once

#include <MathScalar.hpp>

#include <Hash.h>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Vector4.hpp>

#include <cstddef>
#include <type_traits>

namespace rox::math
{
	struct Int4 final
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;
		int32 W = 0;

		constexpr Int4() noexcept = default;

		constexpr Int4(const int32 x, const int32 y, const int32 z, const int32 w) noexcept : X(x), Y(y), Z(z), W(w)
		{
		}

		[[nodiscard]] static constexpr Int4 Zero() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr Int4 One() noexcept
		{
			return {1, 1, 1, 1};
		}

		[[nodiscard]] static constexpr Int4 UnitX() noexcept
		{
			return {1, 0, 0, 0};
		}

		[[nodiscard]] static constexpr Int4 UnitY() noexcept
		{
			return {0, 1, 0, 0};
		}

		[[nodiscard]] static constexpr Int4 UnitZ() noexcept
		{
			return {0, 0, 1, 0};
		}

		[[nodiscard]] static constexpr Int4 UnitW() noexcept
		{
			return {0, 0, 0, 1};
		}

		[[nodiscard]] constexpr Int4 operator+() const noexcept
		{
			return *this;
		}

		[[nodiscard]] constexpr bool operator==(const Int4&) const noexcept = default;

		// Arithmetic requires an int32 result in every lane; divisors must be nonzero.
		[[nodiscard]] Int4 operator-() const noexcept;
		[[nodiscard]] Int4 operator+(const Int4& other) const noexcept;
		[[nodiscard]] Int4 operator-(const Int4& other) const noexcept;
		[[nodiscard]] Int4 operator*(const Int4& other) const noexcept;
		[[nodiscard]] Int4 operator/(const Int4& other) const noexcept;
		[[nodiscard]] Int4 operator*(int32 scalar) const noexcept;
		[[nodiscard]] Int4 operator/(int32 scalar) const noexcept;
		Int4& operator+=(const Int4& other) noexcept;
		Int4& operator-=(const Int4& other) noexcept;
		Int4& operator*=(const Int4& other) noexcept;
		Int4& operator/=(const Int4& other) noexcept;
		Int4& operator*=(int32 scalar) noexcept;
		Int4& operator/=(int32 scalar) noexcept;
		[[nodiscard]] Int4 Min(const Int4& other) const noexcept;
		[[nodiscard]] Int4 Max(const Int4& other) const noexcept;
		[[nodiscard]] Int4 Abs() const noexcept;
		[[nodiscard]] Int4 Clamped(const Int4& minimum, const Int4& maximum) const noexcept;

		// Float conversions require finite inputs and representable rounded int32 lanes. Ties round away from zero.
		template <MathScalar T>
		[[nodiscard]] static Int4 Floor(const Vector4<T>& value) noexcept;

		template <MathScalar T>
		[[nodiscard]] static Int4 Ceil(const Vector4<T>& value) noexcept;

		template <MathScalar T>
		[[nodiscard]] static Int4 Round(const Vector4<T>& value) noexcept;

		// Large integer magnitudes may lose precision in float32.
		template <MathScalar T = float32>
		[[nodiscard]] Vector4<T> ToVector4() const noexcept;
	};

	[[nodiscard]] Int4 operator*(int32 scalar, const Int4& value) noexcept;

	static_assert(sizeof(Int4) == 4 * sizeof(int32) && alignof(Int4) == alignof(int32));
	static_assert(offsetof(Int4, X) == 0 && offsetof(Int4, Y) == sizeof(int32) && offsetof(Int4, Z) == 2 * sizeof(int32) && offsetof(Int4, W) == 3 * sizeof(int32));
	static_assert(core::IsStandardLayout<Int4> && core::IsTriviallyCopyable<Int4>);
} // namespace rox::math

namespace rox::core
{
	template <>
	struct DefaultHash<rox::math::Int4>
	{
		[[nodiscard]] uint64 operator()(const rox::math::Int4& value) const noexcept
		{
			const uint64 seedX = HashCombine(0, DefaultHash<int32>{}(value.X));
			const uint64 seedY = HashCombine(seedX, DefaultHash<int32>{}(value.Y));
			const uint64 seedZ = HashCombine(seedY, DefaultHash<int32>{}(value.Z));
			return HashCombine(seedZ, DefaultHash<int32>{}(value.W));
		}
	};

} // namespace rox::core
