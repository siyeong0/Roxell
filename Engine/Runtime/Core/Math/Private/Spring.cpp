#include <Spring.h>
#include "SpringDetail.hpp"
#include <Assertion.h>
#include <Numeric.hpp>

#include <limits>

namespace rox::math
{
	template <MathScalar T>
	static T springNarrow(T value) noexcept
	{
		ASSERT(rox::core::IsFinite(value),
		       "Interpolation result must fit the finite scalar range.");
		return static_cast<T>(value);
	}

	template <MathScalar T>
	static SpringState<T> springApply(SpringState<T> current, T target, const detail::SpringTransition<T>& transition) noexcept
	{
		const T offset = static_cast<T>(current.Value) - target;
		const T value = rox::core::Abs(transition.ValueDeltaFromValue) < T(0.5)
		                    ? rox::core::Fma(transition.ValueFromVelocity, current.Velocity,
		                                     rox::core::Fma(transition.ValueDeltaFromValue, offset, current.Value))
		                    : rox::core::Fma(transition.ValueFromVelocity, current.Velocity,
		                                     rox::core::Fma(transition.ValueFromValue, offset, target));
		const T velocity = rox::core::Abs(transition.VelocityDeltaFromVelocity) < T(0.5)
		                       ? rox::core::Fma(transition.VelocityFromValue, offset,
		                                        rox::core::Fma(transition.VelocityDeltaFromVelocity, current.Velocity,
		                                                       current.Velocity))
		                       : rox::core::Fma(transition.VelocityFromValue, offset,
		                                        transition.VelocityFromVelocity * current.Velocity);
		return {springNarrow<T>(value), springNarrow<T>(velocity)};
	}

	template <MathScalar T>
	SpringState<T> SpringStep(SpringState<T> current, T target, T frequencyHz, T dampingRatio, T deltaSeconds) noexcept
	{
		ASSERT(rox::core::IsFinite(current.Value) && rox::core::IsFinite(current.Velocity) && rox::core::IsFinite(target), "Spring state and target must be finite.");
		ASSERT(rox::core::IsFinite(frequencyHz) && frequencyHz >= 0 && rox::core::IsFinite(dampingRatio) && dampingRatio >= 0 && rox::core::IsFinite(deltaSeconds) && deltaSeconds >= 0, "Spring parameters must be finite and nonnegative.");

		if (deltaSeconds == 0)
		{
			return current;
		}

		if (frequencyHz == 0)
		{
			return {springNarrow<T>(rox::core::Fma(static_cast<T>(current.Velocity), deltaSeconds, current.Value)),
			        current.Velocity};
		}

		return springApply<T>(current, target, detail::SpringCoefficients<T>(frequencyHz, dampingRatio, deltaSeconds));
	}

	template <MathScalar T, typename TValue>
	static SpringState<TValue> springVectorStep(SpringState<TValue> current, const TValue& target, T frequencyHz, T dampingRatio, T deltaSeconds) noexcept
	{
		ASSERT(current.Value.IsFinite() && current.Velocity.IsFinite() && target.IsFinite(), "Spring state and target must be finite.");
		ASSERT(rox::core::IsFinite(frequencyHz) && frequencyHz >= 0 && rox::core::IsFinite(dampingRatio) && dampingRatio >= 0 && rox::core::IsFinite(deltaSeconds) && deltaSeconds >= 0, "Spring parameters must be finite and nonnegative.");

		if (deltaSeconds == 0)
		{
			return current;
		}

		const detail::SpringTransition<T> transition = frequencyHz == 0
		                                           ? detail::SpringTransition<T>{1, deltaSeconds, 0, 1, 0, 0}
		                                           : detail::SpringCoefficients<T>(frequencyHz, dampingRatio, deltaSeconds);
		SpringState<TValue> result;
		const auto apply = [&](T TValue::* component) noexcept
		{
			const SpringState<T> scalar{current.Value.*component, current.Velocity.*component};
			const SpringState<T> stepped = frequencyHz == 0
			                         ? SpringState<T>{springNarrow<T>(rox::core::Fma(static_cast<T>(scalar.Velocity),
			                                                                         deltaSeconds, scalar.Value)),
			                                          scalar.Velocity}
			                         : springApply<T>(scalar, target.*component, transition);
			result.Value.*component = stepped.Value;
			result.Velocity.*component = stepped.Velocity;
		};
		apply(&TValue::X);
		apply(&TValue::Y);
		if constexpr (requires { TValue::Z; })
		{
			apply(&TValue::Z);
		}

		if constexpr (requires { TValue::W; })
		{
			apply(&TValue::W);
		}

		return result;
	}

	template <MathScalar T>
	SpringState<Vector2<T>> SpringStep(SpringState<Vector2<T>> current, const Vector2<T>& target, T frequencyHz, T dampingRatio, T deltaSeconds) noexcept
	{
		return springVectorStep<T>(current, target, frequencyHz, dampingRatio, deltaSeconds);
	}

	template <MathScalar T>
	SpringState<Vector3<T>> SpringStep(SpringState<Vector3<T>> current, const Vector3<T>& target, T frequencyHz, T dampingRatio, T deltaSeconds) noexcept
	{
		return springVectorStep<T>(current, target, frequencyHz, dampingRatio, deltaSeconds);
	}

	template <MathScalar T>
	SpringState<Vector4<T>> SpringStep(SpringState<Vector4<T>> current, const Vector4<T>& target, T frequencyHz, T dampingRatio, T deltaSeconds) noexcept
	{
		return springVectorStep<T>(current, target, frequencyHz, dampingRatio, deltaSeconds);
	}

	template SpringState<float32> SpringStep(SpringState<float32> current, float32 target, float32 frequencyHz, float32 dampingRatio, float32 deltaSeconds) noexcept;
	template SpringState<float64> SpringStep(SpringState<float64> current, float64 target, float64 frequencyHz, float64 dampingRatio, float64 deltaSeconds) noexcept;
	template SpringState<Vector2<float32>> SpringStep(SpringState<Vector2<float32>> current, const Vector2<float32>& target, float32 frequencyHz, float32 dampingRatio, float32 deltaSeconds) noexcept;
	template SpringState<Vector2<float64>> SpringStep(SpringState<Vector2<float64>> current, const Vector2<float64>& target, float64 frequencyHz, float64 dampingRatio, float64 deltaSeconds) noexcept;
	template SpringState<Vector3<float32>> SpringStep(SpringState<Vector3<float32>> current, const Vector3<float32>& target, float32 frequencyHz, float32 dampingRatio, float32 deltaSeconds) noexcept;
	template SpringState<Vector3<float64>> SpringStep(SpringState<Vector3<float64>> current, const Vector3<float64>& target, float64 frequencyHz, float64 dampingRatio, float64 deltaSeconds) noexcept;
	template SpringState<Vector4<float32>> SpringStep(SpringState<Vector4<float32>> current, const Vector4<float32>& target, float32 frequencyHz, float32 dampingRatio, float32 deltaSeconds) noexcept;
	template SpringState<Vector4<float64>> SpringStep(SpringState<Vector4<float64>> current, const Vector4<float64>& target, float64 frequencyHz, float64 dampingRatio, float64 deltaSeconds) noexcept;
} // namespace rox::math
