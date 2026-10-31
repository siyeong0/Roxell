#pragma once

#include <TypeTraits.hpp>
#include <Types.h>

#include <numbers>

namespace rox::core
{
	template <typename T = float32>
	requires IsFloatingPoint<T>
	inline constexpr T PI = std::numbers::pi_v<T>;

	template <typename T = float32>
	requires IsFloatingPoint<T>
	inline constexpr T TWO_PI = PI<T> * T(2);

	template <typename T = float32>
	requires IsFloatingPoint<T>
	inline constexpr T HALF_PI = PI<T> * T(0.5);

	template <typename T = float32>
	requires IsFloatingPoint<T>
	inline constexpr T EULER = std::numbers::e_v<T>;

	template <typename T = float32>
	requires IsFloatingPoint<T>
	inline constexpr T LN_TWO = std::numbers::ln2_v<T>;
}

namespace rox::math
{
	using core::PI;
	using core::TWO_PI;
	using core::HALF_PI;
	using core::EULER;
	using core::LN_TWO;
}
