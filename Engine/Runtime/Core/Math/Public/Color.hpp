#pragma once

#include <MathScalar.hpp>

#include <Types.h>
#include <Vector3.hpp>

namespace rox::math
{
	struct Color32 final
	{
		uint8 R = 0;
		uint8 G = 0;
		uint8 B = 0;
		uint8 A = 0;

		[[nodiscard]] constexpr bool operator==(const Color32&) const noexcept = default;

		// Numeric bit order is independent of host byte order.
		[[nodiscard]] constexpr uint32 PackRGBA() const noexcept
		{
			return uint32(R) | (uint32(G) << 8) | (uint32(B) << 16) | (uint32(A) << 24);
		}

		[[nodiscard]] constexpr uint32 PackBGRA() const noexcept
		{
			return uint32(B) | (uint32(G) << 8) | (uint32(R) << 16) | (uint32(A) << 24);
		}

		[[nodiscard]] static constexpr Color32 FromRGBA(uint32 packed) noexcept
		{
			return {uint8(packed), uint8(packed >> 8), uint8(packed >> 16), uint8(packed >> 24)};
		}

		[[nodiscard]] static constexpr Color32 FromBGRA(uint32 packed) noexcept
		{
			return {uint8(packed >> 16), uint8(packed >> 8), uint8(packed), uint8(packed >> 24)};
		}
	};

	// Scalar transfer functions require finite normalized channels.
	template <MathScalar T = float32>
	[[nodiscard]] T SRGBToLinear(T channel) noexcept;

	template <MathScalar T = float32>
	[[nodiscard]] T LinearToSRGB(T channel) noexcept;

	template <MathScalar T = float32>
	struct LinearColor final
	{
		T R = T(0.0);
		T G = T(0.0);
		T B = T(0.0);
		T A = T(0.0);

		[[nodiscard]] constexpr bool operator==(const LinearColor<T>&) const noexcept = default;
		[[nodiscard]] bool IsFinite() const noexcept;

		// HSV uses hue turns, normalized saturation and nonnegative HDR value. Finite hue wraps.
		[[nodiscard]] static LinearColor<T> FromHSV(Vector3<T> hsv, T alpha = T(1.0)) noexcept;

		// Requires finite nonnegative RGB. Hue is [0,1); aroxelltic hue and black saturation are zero.
		[[nodiscard]] Vector3<T> ToHSV() const noexcept;
		[[nodiscard]] static LinearColor<T> FromSRGB(Color32 color) noexcept;
		[[nodiscard]] static LinearColor<T> FromLinearUNorm(Color32 color) noexcept;

		// Finite channels clamp to [0,1], then round to nearest with ties upward. Alpha is linear.
		[[nodiscard]] Color32 ToSRGB() const noexcept;
		[[nodiscard]] Color32 ToLinearUNorm() const noexcept;

		[[nodiscard]] constexpr LinearColor<T> Premultiplied() const noexcept
		{
			return {R * A, G * A, B * A, A};
		}

		[[nodiscard]] constexpr T Luminance() const noexcept
		{
			return T(0.2126) * R + T(0.7152) * G + T(0.0722) * B;
		}

		[[nodiscard]] constexpr LinearColor<T> operator+(const LinearColor<T>& other) const noexcept
		{
			return {R + other.R, G + other.G, B + other.B, A + other.A};
		}

		[[nodiscard]] constexpr LinearColor<T> operator*(const LinearColor<T>& other) const noexcept
		{
			return {R * other.R, G * other.G, B * other.B, A * other.A};
		}

		[[nodiscard]] constexpr LinearColor<T> operator*(T scale) const noexcept
		{
			return {R * scale, G * scale, B * scale, A * scale};
		}

		[[nodiscard]] static LinearColor<T> Lerp(const LinearColor<T>& from, const LinearColor<T>& to, T alpha) noexcept;
	};
} // namespace rox::math
