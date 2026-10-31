#pragma once

#include <MathScalar.hpp>

#include <Hash.h>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Vector3.hpp>

#include <cstddef>
#include <type_traits>

namespace rox::math
{
	struct Int3 final
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;

		constexpr Int3() noexcept = default;

		constexpr Int3(const int32 x, const int32 y, const int32 z) noexcept : X(x), Y(y), Z(z)
		{
		}

		[[nodiscard]] static constexpr Int3 Zero() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr Int3 One() noexcept
		{
			return {1, 1, 1};
		}

		[[nodiscard]] static constexpr Int3 UnitX() noexcept
		{
			return {1, 0, 0};
		}

		[[nodiscard]] static constexpr Int3 UnitY() noexcept
		{
			return {0, 1, 0};
		}

		[[nodiscard]] static constexpr Int3 UnitZ() noexcept
		{
			return {0, 0, 1};
		}

		[[nodiscard]] constexpr Int3 operator+() const noexcept
		{
			return *this;
		}

		[[nodiscard]] constexpr bool operator==(const Int3&) const noexcept = default;

		// Arithmetic requires an int32 result in every lane; divisors must be nonzero.
		[[nodiscard]] Int3 operator-() const noexcept;
		[[nodiscard]] Int3 operator+(const Int3& other) const noexcept;
		[[nodiscard]] Int3 operator-(const Int3& other) const noexcept;
		[[nodiscard]] Int3 operator*(const Int3& other) const noexcept;
		[[nodiscard]] Int3 operator/(const Int3& other) const noexcept;
		[[nodiscard]] Int3 operator*(int32 scalar) const noexcept;
		[[nodiscard]] Int3 operator/(int32 scalar) const noexcept;
		Int3& operator+=(const Int3& other) noexcept;
		Int3& operator-=(const Int3& other) noexcept;
		Int3& operator*=(const Int3& other) noexcept;
		Int3& operator/=(const Int3& other) noexcept;
		Int3& operator*=(int32 scalar) noexcept;
		Int3& operator/=(int32 scalar) noexcept;
		[[nodiscard]] Int3 Min(const Int3& other) const noexcept;
		[[nodiscard]] Int3 Max(const Int3& other) const noexcept;
		[[nodiscard]] Int3 Abs() const noexcept;
		[[nodiscard]] Int3 Clamped(const Int3& minimum, const Int3& maximum) const noexcept;

		// Float conversions require finite inputs and representable rounded int32 lanes. Ties round away from zero.
		template <MathScalar T>
		[[nodiscard]] static Int3 Floor(const Vector3<T>& value) noexcept;

		template <MathScalar T>
		[[nodiscard]] static Int3 Ceil(const Vector3<T>& value) noexcept;

		template <MathScalar T>
		[[nodiscard]] static Int3 Round(const Vector3<T>& value) noexcept;

		// Large integer magnitudes may lose precision in float32.
		template <MathScalar T = float32>
		[[nodiscard]] Vector3<T> ToVector3() const noexcept;
	};

	[[nodiscard]] Int3 operator*(int32 scalar, const Int3& value) noexcept;

	static_assert(sizeof(Int3) == 3 * sizeof(int32) && alignof(Int3) == alignof(int32));
	static_assert(offsetof(Int3, X) == 0 && offsetof(Int3, Y) == sizeof(int32) && offsetof(Int3, Z) == 2 * sizeof(int32));
	static_assert(rox::core::IsStandardLayout<Int3> && rox::core::IsTriviallyCopyable<Int3>);
} // namespace rox::math

namespace rox::core
{
	template <>
	struct DefaultHash<rox::math::Int3>
	{
		[[nodiscard]] uint64 operator()(const rox::math::Int3& value) const noexcept
		{
			const uint64 seedX = HashCombine(0, DefaultHash<int32>{}(value.X));
			const uint64 seedY = HashCombine(seedX, DefaultHash<int32>{}(value.Y));
			return HashCombine(seedY, DefaultHash<int32>{}(value.Z));
		}
	};

} // namespace rox::core
