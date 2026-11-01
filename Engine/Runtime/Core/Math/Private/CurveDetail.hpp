#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <CurveMath.h>
#include <Numeric.hpp>
#include <TypeTraits.hpp>

#include <cmath>
#include <limits>
#include <type_traits>

namespace rox::math::detail
{
	template <MathScalar T>
	struct CurveSample final
	{
		T Components[4]{};
	};
	enum class ECurveKind : uint8
	{
		Quadratic,
		Cubic,
		Hermite,
		CatmullRom
	};

	template <MathScalar T>
	inline void CurveValidateParameter(T t) noexcept
	{
		ASSERT(core::IsFinite(t) && t >= 0 && t <= 1, "Curve parameter must be finite and between zero and one.");
	}

	template <MathScalar T, typename TValue>
	CurveSample<T> CurveRead(const TValue& value) noexcept
	{
		if constexpr (core::IsSame<TValue, T>)
		{
			ASSERT(core::IsFinite(value), "Curve controls must be finite.");
			return {{value, 0, 0, 0}};
		}
		else
		{
			ASSERT(value.IsFinite(), "Curve controls must be finite.");
			CurveSample<T> result{{value.X, value.Y, 0, 0}};
			if constexpr (requires { value.Z; })
			{
				result.Components[2] = value.Z;
			}

			if constexpr (requires { value.W; })
			{
				result.Components[3] = value.W;
			}

			return result;
		}
	}

	template <MathScalar T, typename TValue>
	TValue CurveNarrow(const CurveSample<T>& value) noexcept
	{
		for (const T component : value.Components)
		{
			ASSERT(core::IsFinite(component), "Interpolation result must fit the finite scalar range.");
		}

		if constexpr (core::IsSame<TValue, T>)
		{
			return static_cast<T>(value.Components[0]);
		}
		else
		{
			TValue result;
			result.X = static_cast<T>(value.Components[0]);
			result.Y = static_cast<T>(value.Components[1]);
			if constexpr (requires { result.Z; })
			{
				result.Z = static_cast<T>(value.Components[2]);
			}

			if constexpr (requires { result.W; })
			{
				result.W = static_cast<T>(value.Components[3]);
			}

			return result;
		}
	}

	template <MathScalar T>
	inline CurveSample<T> CurveEvaluate(ECurveKind kind, const CurveSample<T>* controls, T t, bool derivative) noexcept
	{
		CurveSample<T> result;

		for (uint32 index = 0; index < 4; ++index)
		{
			const T a = controls[0].Components[index];
			const T b = controls[1].Components[index];
			const T c = controls[2].Components[index];

			if (kind == ECurveKind::Quadratic)
			{
				result.Components[index] =
				    derivative ? 2 * core::LerpValue(b - a, c - b, t)
				               : core::LerpValue(core::LerpValue(a, b, t), core::LerpValue(b, c, t), t);
			}
			else
			{
				const T d = controls[3].Components[index];

				if (kind == ECurveKind::Cubic)
				{
					result.Components[index] =
					    derivative
					        ? 3 * core::LerpValue(core::LerpValue(b - a, c - b, t),
					                                   core::LerpValue(c - b, d - c, t), t)
					        : core::LerpValue(
					              core::LerpValue(core::LerpValue(a, b, t), core::LerpValue(b, c, t), t),
					              core::LerpValue(core::LerpValue(b, c, t), core::LerpValue(c, d, t), t),
					              t);
				}
				else
				{
					const T u = 1 - t;
					const T start = kind == ECurveKind::CatmullRom ? b : a;
					const T tangent0 = kind == ECurveKind::CatmullRom ? (c - a) * T(0.5) : b;
					const T tangent1 = kind == ECurveKind::CatmullRom ? (d - b) * T(0.5) : d;
					result.Components[index] = derivative ? (c - start) * (6 * t * u) + tangent0 * (u * (1 - 3 * t)) +
					                                            tangent1 * (t * (3 * t - 2))
					                                      : core::LerpValue(start, c, t * t * (3 - 2 * t)) +
					                                            tangent0 * (t * u * u) - tangent1 * (t * t * u);
				}
			}
		}

		return result;
	}
} // namespace rox::math::detail
