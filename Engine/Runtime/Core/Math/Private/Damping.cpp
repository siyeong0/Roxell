#include <Damping.h>
#include <MathConstants.hpp>
#include <Assertion.h>
#include <Numeric.hpp>
#include <TypeTraits.hpp>

#include <cmath>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T>
	static T dampingWeight(T halfLifeSeconds, T deltaSeconds) noexcept
	{
		return -rox::core::Expm1(-rox::core::LN_TWO<T> * static_cast<T>(deltaSeconds) / halfLifeSeconds);
	}

	template <MathScalar T, typename TValue>
	static TValue dampingApply(const TValue& current, const TValue& target, T halfLifeSeconds, T deltaSeconds) noexcept
	{
		if constexpr (rox::core::IsSame<TValue, T>)
		{
			ASSERT(rox::core::IsFinite(current) && rox::core::IsFinite(target), "Damping inputs must be finite.");
		}
		else
		{
			ASSERT(current.IsFinite() && target.IsFinite(), "Damping inputs must be finite.");
		}

		ASSERT(rox::core::IsFinite(halfLifeSeconds) && halfLifeSeconds >= 0 && rox::core::IsFinite(deltaSeconds) &&
		           deltaSeconds >= 0,
		       "Damping times must be finite and nonnegative.");

		if (deltaSeconds == 0)
		{
			return current;
		}

		if (halfLifeSeconds == 0)
		{
			return target;
		}

		const T weight = dampingWeight<T>(halfLifeSeconds, deltaSeconds);
		const auto blend = [weight](T from, T to) noexcept
		{
			return static_cast<T>(rox::core::LerpValue(static_cast<T>(from), static_cast<T>(to), weight));
		};
		if constexpr (rox::core::IsSame<TValue, T>)
		{
			return blend(current, target);
		}
		else
		{
			TValue result;
			result.X = blend(current.X, target.X);
			result.Y = blend(current.Y, target.Y);
			if constexpr (requires { result.Z; })
			{
				result.Z = blend(current.Z, target.Z);
			}

			if constexpr (requires { result.W; })
			{
				result.W = blend(current.W, target.W);
			}

			return result;
		}
	}

	template <MathScalar T>
	T ExponentialDamp(T current, T target, T halfLifeSeconds, T deltaSeconds) noexcept
	{
		return dampingApply<T>(current, target, halfLifeSeconds, deltaSeconds);
	}

	template <MathScalar T>
	Vector2<T> ExponentialDamp(const Vector2<T>& current, const Vector2<T>& target, T halfLifeSeconds, T deltaSeconds) noexcept
	{
		return dampingApply<T>(current, target, halfLifeSeconds, deltaSeconds);
	}

	template <MathScalar T>
	Vector3<T> ExponentialDamp(const Vector3<T>& current, const Vector3<T>& target, T halfLifeSeconds, T deltaSeconds) noexcept
	{
		return dampingApply<T>(current, target, halfLifeSeconds, deltaSeconds);
	}

	template <MathScalar T>
	Vector4<T> ExponentialDamp(const Vector4<T>& current, const Vector4<T>& target, T halfLifeSeconds, T deltaSeconds) noexcept
	{
		return dampingApply<T>(current, target, halfLifeSeconds, deltaSeconds);
	}

	template float32 ExponentialDamp(float32 current, float32 target, float32 halfLifeSeconds, float32 deltaSeconds) noexcept;
	template float64 ExponentialDamp(float64 current, float64 target, float64 halfLifeSeconds, float64 deltaSeconds) noexcept;
	template Vector2<float32> ExponentialDamp(const Vector2<float32>& current, const Vector2<float32>& target, float32 halfLifeSeconds, float32 deltaSeconds) noexcept;
	template Vector2<float64> ExponentialDamp(const Vector2<float64>& current, const Vector2<float64>& target, float64 halfLifeSeconds, float64 deltaSeconds) noexcept;
	template Vector3<float32> ExponentialDamp(const Vector3<float32>& current, const Vector3<float32>& target, float32 halfLifeSeconds, float32 deltaSeconds) noexcept;
	template Vector3<float64> ExponentialDamp(const Vector3<float64>& current, const Vector3<float64>& target, float64 halfLifeSeconds, float64 deltaSeconds) noexcept;
	template Vector4<float32> ExponentialDamp(const Vector4<float32>& current, const Vector4<float32>& target, float32 halfLifeSeconds, float32 deltaSeconds) noexcept;
	template Vector4<float64> ExponentialDamp(const Vector4<float64>& current, const Vector4<float64>& target, float64 halfLifeSeconds, float64 deltaSeconds) noexcept;
} // namespace rox::math
