#pragma once

#include <MathScalar.hpp>
#include <Types.h>

namespace rox::math
{
	enum class EEasingCurve : uint8
	{
		Linear,
		Quadratic,
		Cubic,
		Quartic,
		Quintic,
		Sine,
		Exponential,
		Circular,
		Back,
		Elastic,
		Bounce
	};
	enum class EEasingMode : uint8
	{
		In,
		Out,
		InOut
	};

	// t is finite and in [0,1]. Back and Elastic preserve overshoot; InOut reflects the In curve.
	template <MathScalar T = float32>
	[[nodiscard]] T Ease(T t, EEasingCurve curve, EEasingMode mode = EEasingMode::InOut) noexcept;
} // namespace rox::math
