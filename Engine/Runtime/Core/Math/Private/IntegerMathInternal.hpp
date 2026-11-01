#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Types.h>

#include <cmath>
#include <limits>

namespace rox::math::detail
{
	[[nodiscard]] inline int32 NarrowIntegerLane(const int64 value) noexcept
	{
		ASSERT(value >= core::NumericLimits<int32>::min() && value <= core::NumericLimits<int32>::max(), "Integer vector result must be representable as int32.");
		return static_cast<int32>(value);
	}

	template <MathScalar T>
	[[nodiscard]] inline bool IsIntegerConversionRepresentable(const T rounded) noexcept
	{
		return core::IsFinite(rounded) && rounded >= static_cast<T>(core::NumericLimits<int32>::min()) && rounded < T(2147483648.0);
	}
} // namespace rox::math::detail
