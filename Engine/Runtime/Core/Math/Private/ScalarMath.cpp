#include <ScalarMath.h>
#include <MathConstants.hpp>
#include <Numeric.hpp>
#include <Comparison.hpp>

#include <Assertion.h>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	template <MathScalar T>
	T Clamp(T value, T minimum, T maximum) noexcept
	{
		ASSERT(minimum <= maximum, "Clamp requires an ordered interval.");
		return core::ClampValue(value, minimum, maximum);
	}

	template <MathScalar T>
	T Saturate(T value) noexcept
	{
		return Clamp<T>(value, T(0.0), T(1.0));
	}

	template <MathScalar T>
	T Lerp(T from, T to, T alpha) noexcept
	{
		return core::LerpValue(from, to, alpha);
	}

	template <MathScalar T>
	T InverseLerp(T from, T to, T value) noexcept
	{
		ASSERT(from != to, "Inverse interpolation requires distinct endpoints.");
		const T numerator = value - from;
		const T denominator = to - from;
		if (core::IsFinite(numerator) && core::IsFinite(denominator))
		{
			return numerator / denominator;
		}

		// Halving preserves the differences when opposite finite endpoints overflow subtraction.
		return (value * T(0.5) - from * T(0.5)) / (to * T(0.5) - from * T(0.5));
	}

	template <MathScalar T>
	T Remap(T value, T inputMinimum, T inputMaximum, T outputMinimum, T outputMaximum) noexcept
	{
		const T alpha = InverseLerp(inputMinimum, inputMaximum, value);
		return core::LerpValue(outputMinimum, outputMaximum, alpha);
	}

	template <MathScalar T>
	bool NearlyEqual(T left, T right, T absoluteTolerance, T relativeTolerance) noexcept
	{
		ASSERT(core::IsFinite(absoluteTolerance) && absoluteTolerance >= T(0.0) && core::IsFinite(relativeTolerance) && relativeTolerance >= T(0.0), "Comparison tolerances must be finite and nonnegative.");

		if (left == right)
		{
			return true;
		}

		if (!core::IsFinite(left) || !core::IsFinite(right))
		{
			return false;
		}

		const T difference = core::Abs(static_cast<T>(left) - right);
		return difference <= absoluteTolerance ||
		       difference <= relativeTolerance * core::Max(core::Abs(static_cast<T>(left)),
		                                                        core::Abs(static_cast<T>(right)));
	}

	template <MathScalar T>
	bool IsNearlyZero(T value, T tolerance) noexcept
	{
		ASSERT(core::IsFinite(tolerance) && tolerance >= T(0.0), "Zero comparison tolerance must be finite and nonnegative.");
		return core::Abs(value) <= tolerance;
	}

	template <MathScalar T>
	T Wrap(T value, T minimum, T maximum) noexcept
	{
		ASSERT(core::IsFinite(value) && core::IsFinite(minimum) && core::IsFinite(maximum) && minimum < maximum, "Wrapping requires finite input and an increasing interval.");

		if (value >= minimum && value < maximum)
		{
			return value;
		}

		const T width = static_cast<T>(maximum) - minimum;
		T offset = core::Fmod(static_cast<T>(value) - minimum, width);

		if (offset < T(0.0))
		{
			offset += width;
		}

		const T wrapped = static_cast<T>(minimum + offset);
		return wrapped >= maximum ? minimum : wrapped;
	}

	template <MathScalar T>
	T Repeat(T value, T length) noexcept
	{
		ASSERT(length > T(0.0), "Repeat length must be positive.");
		return Wrap<T>(value, T(0.0), length);
	}

	template <MathScalar T>
	T PingPong(T value, T length) noexcept
	{
		ASSERT(core::IsFinite(value) && core::IsFinite(length) && length > T(0.0), "Ping-pong requires finite input and a positive finite length.");
		const T period = T(2.0) * length;
		const T phase = core::Fmod(core::Abs(static_cast<T>(value)), period);
		return static_cast<T>(phase <= length ? phase : period - phase);
	}

	template <MathScalar T>
	T DeltaAngle(T from, T to) noexcept
	{
		constexpr T pi = core::PI<T>;
		constexpr T period = T(2) * pi;
		T delta = to - from;
		if (delta >= -pi && delta < pi)
		{
			return delta;
		}

		if (!core::IsFinite(delta))
		{
			ASSERT(core::IsFinite(from) && core::IsFinite(to), "Angles must be finite.");
			delta = core::Fmod(to, period) - core::Fmod(from, period);
		}

		delta = core::Fmod(delta, period);
		if (delta >= pi)
		{
			delta -= period;
		}
		else if (delta < -pi)
		{
			delta += period;
		}

		return delta;
	}

	template <MathScalar T>
	T LerpAngle(T from, T to, T alpha) noexcept
	{
		return from + DeltaAngle<T>(from, to) * alpha;
	}

	template <MathScalar T>
	T MoveTowards(T current, T target, T maximumDelta) noexcept
	{
		ASSERT(core::IsFinite(current) && core::IsFinite(target), "Movement endpoints must be finite.");
		ASSERT(core::IsFinite(maximumDelta) && maximumDelta >= T(0.0), "Maximum movement must be finite and nonnegative.");
		const T delta = static_cast<T>(target) - current;
		return core::Abs(delta) <= maximumDelta
		           ? target
		           : static_cast<T>(current + core::CopySign(static_cast<T>(maximumDelta), delta));
	}

	template <MathScalar T>
	T SmoothStep(T edge0, T edge1, T value) noexcept
	{
		const T alpha = Saturate<T>(InverseLerp<T>(edge0, edge1, value));
		return alpha * alpha * (T(3.0) - T(2.0) * alpha);
	}

	template <MathScalar T>
	T SmootherStep(T edge0, T edge1, T value) noexcept
	{
		const T alpha = Saturate<T>(InverseLerp<T>(edge0, edge1, value));
		return alpha * alpha * alpha * (alpha * (alpha * T(6.0) - T(15.0)) + T(10.0));
	}

	template <MathScalar T>
	T Snap(T value, T step) noexcept
	{
		ASSERT(core::IsFinite(step) && step > T(0.0), "Snap step must be positive and finite.");
		return static_cast<T>(core::Round(static_cast<T>(value) / step) * step);
	}

	template float32 Clamp(float32 value, float32 minimum, float32 maximum) noexcept;
	template float64 Clamp(float64 value, float64 minimum, float64 maximum) noexcept;
	template float32 Saturate(float32 value) noexcept;
	template float64 Saturate(float64 value) noexcept;
	template float32 Lerp(float32 from, float32 to, float32 alpha) noexcept;
	template float64 Lerp(float64 from, float64 to, float64 alpha) noexcept;
	template float32 InverseLerp(float32 from, float32 to, float32 value) noexcept;
	template float64 InverseLerp(float64 from, float64 to, float64 value) noexcept;
	template float32 Remap(float32 value, float32 inputMinimum, float32 inputMaximum, float32 outputMinimum, float32 outputMaximum) noexcept;
	template float64 Remap(float64 value, float64 inputMinimum, float64 inputMaximum, float64 outputMinimum, float64 outputMaximum) noexcept;
	template bool NearlyEqual(float32 left, float32 right, float32 absoluteTolerance, float32 relativeTolerance) noexcept;
	template bool NearlyEqual(float64 left, float64 right, float64 absoluteTolerance, float64 relativeTolerance) noexcept;
	template bool IsNearlyZero(float32 value, float32 tolerance) noexcept;
	template bool IsNearlyZero(float64 value, float64 tolerance) noexcept;
	template float32 Wrap(float32 value, float32 minimum, float32 maximum) noexcept;
	template float64 Wrap(float64 value, float64 minimum, float64 maximum) noexcept;
	template float32 Repeat(float32 value, float32 length) noexcept;
	template float64 Repeat(float64 value, float64 length) noexcept;
	template float32 PingPong(float32 value, float32 length) noexcept;
	template float64 PingPong(float64 value, float64 length) noexcept;
	template float32 DeltaAngle(float32 from, float32 to) noexcept;
	template float64 DeltaAngle(float64 from, float64 to) noexcept;
	template float32 LerpAngle(float32 from, float32 to, float32 alpha) noexcept;
	template float64 LerpAngle(float64 from, float64 to, float64 alpha) noexcept;
	template float32 MoveTowards(float32 current, float32 target, float32 maximumDelta) noexcept;
	template float64 MoveTowards(float64 current, float64 target, float64 maximumDelta) noexcept;
	template float32 SmoothStep(float32 edge0, float32 edge1, float32 value) noexcept;
	template float64 SmoothStep(float64 edge0, float64 edge1, float64 value) noexcept;
	template float32 SmootherStep(float32 edge0, float32 edge1, float32 value) noexcept;
	template float64 SmootherStep(float64 edge0, float64 edge1, float64 value) noexcept;
	template float32 Snap(float32 value, float32 step) noexcept;
	template float64 Snap(float64 value, float64 step) noexcept;
} // namespace rox::math
