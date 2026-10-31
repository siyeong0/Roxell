#include <Color.hpp>
#include <Numeric.hpp>
#include <Comparison.hpp>
#include <Assertion.h>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		[[nodiscard]] uint8 quantize(T channel) noexcept
		{
			return static_cast<uint8>(rox::core::Floor(rox::core::ClampValue(channel, T(0.0), T(1.0)) * T(255.0) + T(0.5)));
		}
	} // namespace

	template <MathScalar T>
	T SRGBToLinear(T channel) noexcept
	{
		ASSERT(rox::core::IsFinite(channel) && channel >= T(0.0) && channel <= T(1.0), "sRGB decoding requires a finite normalized channel.");
		return channel <= T(0.04045) ? channel / T(12.92) : rox::core::Pow((channel + T(0.055)) / T(1.055), T(2.4));
	}

	template <MathScalar T>
	T LinearToSRGB(T channel) noexcept
	{
		ASSERT(rox::core::IsFinite(channel) && channel >= T(0.0) && channel <= T(1.0), "sRGB encoding requires a finite normalized channel.");
		return channel <= T(0.0031308) ? channel * T(12.92) : T(1.055) * rox::core::Pow(channel, T(1.0) / T(2.4)) - T(0.055);
	}

	template <MathScalar T>
	bool LinearColor<T>::IsFinite() const noexcept
	{
		return rox::core::IsFinite(R) && rox::core::IsFinite(G) && rox::core::IsFinite(B) && rox::core::IsFinite(A);
	}

	template <MathScalar T>
	LinearColor<T> LinearColor<T>::FromHSV(Vector3<T> hsv, T alpha) noexcept
	{
		ASSERT(hsv.IsFinite() && hsv.Y >= T(0.0) && hsv.Y <= T(1.0) && hsv.Z >= T(0.0) && rox::core::IsFinite(alpha), "HSV conversion requires finite channels, normalized saturation and nonnegative value.");
		const T hue = (T(hsv.X) - rox::core::Floor(T(hsv.X))) * T(6.0);
		const int sector = static_cast<int>(hue);
		const T fraction = hue - sector;
		const T low = static_cast<T>(T(hsv.Z) * (T(1.0) - hsv.Y));
		const T falling = static_cast<T>(T(hsv.Z) * (T(1.0) - hsv.Y * fraction));
		const T rising = static_cast<T>(T(hsv.Z) * (T(1.0) - hsv.Y * (T(1.0) - fraction)));

		switch (sector % 6)
		{
		case 0:
			return {hsv.Z, rising, low, alpha};
		case 1:
			return {falling, hsv.Z, low, alpha};
		case 2:
			return {low, hsv.Z, rising, alpha};
		case 3:
			return {low, falling, hsv.Z, alpha};
		case 4:
			return {rising, low, hsv.Z, alpha};
		default:
			return {hsv.Z, low, falling, alpha};
		}
	}

	template <MathScalar T>
	Vector3<T> LinearColor<T>::ToHSV() const noexcept
	{
		ASSERT(IsFinite() && R >= T(0.0) && G >= T(0.0) && B >= T(0.0), "HSV conversion requires finite nonnegative RGB channels and finite alpha.");
		const T maximum = rox::core::Max({T(R), T(G), T(B)});
		const T minimum = rox::core::Min({T(R), T(G), T(B)});
		const T delta = maximum - minimum;

		if (delta == T(0.0))
		{
			return {T(0.0), T(0.0), static_cast<T>(maximum)};
		}

		T hue = maximum == R   ? (T(G) - B) / delta
		        : maximum == G ? (T(B) - R) / delta + T(2.0)
		                       : (T(R) - G) / delta + T(4.0);
		hue /= T(6.0);

		if (hue < T(0.0))
		{
			hue += T(1.0);
		}

		const T wrappedHue = static_cast<T>(hue);
		return {wrappedHue < T(1.0) ? wrappedHue : T(0.0), static_cast<T>(delta / maximum), static_cast<T>(maximum)};
	}

	template <MathScalar T>
	LinearColor<T> LinearColor<T>::FromSRGB(Color32 color) noexcept
	{
		return {SRGBToLinear<T>(color.R / T(255.0)), SRGBToLinear<T>(color.G / T(255.0)), SRGBToLinear<T>(color.B / T(255.0)), color.A / T(255.0)};
	}

	template <MathScalar T>
	LinearColor<T> LinearColor<T>::FromLinearUNorm(Color32 color) noexcept
	{
		return {color.R / T(255.0), color.G / T(255.0), color.B / T(255.0), color.A / T(255.0)};
	}

	template <MathScalar T>
	Color32 LinearColor<T>::ToSRGB() const noexcept
	{
		ASSERT(IsFinite(), "Color quantization requires finite channels.");
		return {quantize<T>(LinearToSRGB<T>(rox::core::ClampValue(R, T(0.0), T(1.0)))),
		        quantize<T>(LinearToSRGB<T>(rox::core::ClampValue(G, T(0.0), T(1.0)))),
		        quantize<T>(LinearToSRGB<T>(rox::core::ClampValue(B, T(0.0), T(1.0)))), quantize<T>(A)};
	}

	template <MathScalar T>
	Color32 LinearColor<T>::ToLinearUNorm() const noexcept
	{
		ASSERT(IsFinite(), "Color quantization requires finite channels.");
		return {quantize<T>(R), quantize<T>(G), quantize<T>(B), quantize<T>(A)};
	}

	template <MathScalar T>
	LinearColor<T> LinearColor<T>::Lerp(const LinearColor<T>& from, const LinearColor<T>& to, T alpha) noexcept
	{
		return {rox::core::LerpValue(from.R, to.R, alpha), 
				rox::core::LerpValue(from.G, to.G, alpha),
		        rox::core::LerpValue(from.B, to.B, alpha),
				rox::core::LerpValue(from.A, to.A, alpha)};
	}

	template float32 SRGBToLinear(float32 channel) noexcept;
	template float64 SRGBToLinear(float64 channel) noexcept;
	template float32 LinearToSRGB(float32 channel) noexcept;
	template float64 LinearToSRGB(float64 channel) noexcept;

	template struct LinearColor<float32>;
	template struct LinearColor<float64>;
} // namespace rox::math
