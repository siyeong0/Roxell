#pragma once

#include <MathConstants.hpp>

#include <MathScalar.hpp>
#include <Numeric.hpp>
#include <Types.h>

#include <cmath>

namespace rox::math::detail
{
	template <MathScalar T>
	struct SpringTransition final
	{
		T ValueFromValue;
		T ValueFromVelocity;
		T VelocityFromValue;
		T VelocityFromVelocity;

		T ValueDeltaFromValue;
		T VelocityDeltaFromVelocity;
	};

	template <MathScalar T>
	inline SpringTransition<T> SpringCoefficients(T frequencyHz, T dampingRatio, T deltaSeconds) noexcept
	{
		const T w = 2 * rox::core::PI<T> * frequencyHz;
		const T ratio = dampingRatio;
		const T dt = deltaSeconds;
		const T a = ratio * w;

		if (w * dt < T(0.001) && a * dt < T(0.001))
		{
			const T k10 = -w * w * dt;
			const T k11 = -2 * a * dt;
			SpringTransition<T> term{0, dt, k10, k11, 0, 0};
			SpringTransition<T> sum = term;
			// Accumulate the change directly: a diagonal near one can hide a representable displacement.
			for (uint32 order = 2; order <= 8; ++order)
			{
				const SpringTransition<T> next{term.ValueFromVelocity * k10 / order,
				                               (term.ValueFromValue * dt + term.ValueFromVelocity * k11) / order,
				                               term.VelocityFromVelocity * k10 / order,
				                               (term.VelocityFromValue * dt + term.VelocityFromVelocity * k11) / order,
				                               0,
				                               0};
				term = next;
				sum.ValueFromValue += term.ValueFromValue;
				sum.ValueFromVelocity += term.ValueFromVelocity;
				sum.VelocityFromValue += term.VelocityFromValue;
				sum.VelocityFromVelocity += term.VelocityFromVelocity;
			}

			sum.ValueDeltaFromValue = sum.ValueFromValue;
			sum.VelocityDeltaFromVelocity = sum.VelocityFromVelocity;
			sum.ValueFromValue += 1;
			sum.VelocityFromVelocity += 1;
			return sum;
		}

		if (ratio == 1)
		{
			const T e = rox::core::Exp(-w * dt);
			const T delta = rox::core::Expm1(-w * dt);
			const T s = dt * e;
			return {e + w * s, s, -w * w * s, e - w * s, delta + w * s, delta - w * s};
		}

		if (ratio < 1)
		{
			const T b = w * rox::core::Sqrt((1 - ratio) * (1 + ratio));
			const T e = rox::core::Exp(-a * dt);
			const T c = e * rox::core::Cos(b * dt);
			const T s = e * rox::core::Sin(b * dt) / b;
			const T halfSine = rox::core::Sin(b * dt * T(0.5));
			const T delta = rox::core::Expm1(-a * dt) - 2 * e * halfSine * halfSine;
			return {c + a * s, s, -w * w * s, c - a * s, delta + a * s, delta - a * s};
		}

		const T root = rox::core::Sqrt((ratio - 1) * (ratio + 1));
		const T slow = -w / (ratio + root);
		const T fast = -w * (ratio + root);
		const T separation = slow - fast;
		const T slowExp = rox::core::Exp(slow * dt);
		const T fastExp = rox::core::Exp(fast * dt);
		const T s = -slowExp * rox::core::Expm1(-separation * dt) / separation;
		return {slowExp - slow * s,
		        s,
		        -w * w * s,
		        fastExp + slow * s,
		        rox::core::Expm1(slow * dt) - slow * s,
		        rox::core::Expm1(fast * dt) + slow * s};
	}
} // namespace rox::math::detail
