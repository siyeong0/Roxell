#include <Easing.h>
#include <MathConstants.hpp>
#include <Assertion.h>
#include <Numeric.hpp>

#include <cmath>

namespace rox::math
{
	template <MathScalar T>
	static T easingBounceOut(T t) noexcept
	{
		if (t < 1 / T(2.75))
		{
			return T(7.5625) * t * t;
		}

		if (t < 2 / T(2.75))
		{
			const T shifted = t - T(1.5) / T(2.75);
			return T(7.5625) * shifted * shifted + T(0.75);
		}

		if (t < T(2.5) / T(2.75))
		{
			const T shifted = t - T(2.25) / T(2.75);
			return T(7.5625) * shifted * shifted + T(0.9375);
		}

		const T shifted = t - T(2.625) / T(2.75);
		return T(7.5625) * shifted * shifted + T(0.984375);
	}

	template <MathScalar T>
	static T easingIn(T t, EEasingCurve curve) noexcept
	{
		if (t == 0 || t == 1)
		{
			return t;
		}

		if (curve == EEasingCurve::Linear)
		{
			return t;
		}

		if (curve == EEasingCurve::Quadratic)
		{
			return t * t;
		}

		if (curve == EEasingCurve::Cubic)
		{
			return t * t * t;
		}

		if (curve == EEasingCurve::Quartic)
		{
			return t * t * t * t;
		}

		if (curve == EEasingCurve::Quintic)
		{
			return t * t * t * t * t;
		}

		if (curve == EEasingCurve::Sine)
		{
			const T sine = rox::core::Sin(rox::core::PI<T> * t / 4);
			return 2 * sine * sine;
		}

		if (curve == EEasingCurve::Exponential)
		{
			return rox::core::Expm1(10 * rox::core::LN_TWO<T> * t) / 1023;
		}

		if (curve == EEasingCurve::Circular)
		{
			return t * t / (1 + rox::core::Sqrt(1 - t * t));
		}

		if (curve == EEasingCurve::Back)
		{
			return t * t * (T(2.70158) * t - T(1.70158));
		}

		if (curve == EEasingCurve::Elastic)
		{
			return -rox::core::Exp2(10 * t - 10) * rox::core::Sin((10 * t - T(10.75)) * 2 * rox::core::PI<T> / 3);
		}

		return 1 - easingBounceOut<T>(1 - t);
	}

	template <MathScalar T>
	T Ease(T t, EEasingCurve curve, EEasingMode mode) noexcept
	{
		ASSERT(rox::core::IsFinite(t) && t >= 0 && t <= 1, "Easing parameter must be finite and between zero and one.");
		ASSERT(curve <= EEasingCurve::Bounce, "Easing curve must be a listed value.");
		ASSERT(mode <= EEasingMode::InOut, "Easing mode must be a listed value.");

		if (t == 0 || t == 1)
		{
			return t;
		}

		const T parameter = t;

		if (mode == EEasingMode::In)
		{
			return static_cast<T>(easingIn<T>(parameter, curve));
		}

		if (mode == EEasingMode::Out)
		{
			return static_cast<T>(1 - easingIn<T>(1 - parameter, curve));
		}

		return static_cast<T>(parameter < T(0.5) ? easingIn<T>(2 * parameter, curve) * T(0.5)
		                                         : 1 - easingIn<T>(2 - 2 * parameter, curve) * T(0.5));
	}

	template float32 Ease(float32 t, EEasingCurve curve, EEasingMode mode) noexcept;
	template float64 Ease(float64 t, EEasingCurve curve, EEasingMode mode) noexcept;
} // namespace rox::math
