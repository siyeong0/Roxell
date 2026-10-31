#pragma once

#include <cfenv>
#include <utility>

namespace rox::core
{
	using FloatEnvironment = std::fenv_t;

	template <typename... TArgs>
	constexpr auto SetFloatEnvironment(TArgs&&... args) noexcept(noexcept(std::fesetenv(std::forward<TArgs>(args)...)))
	    -> decltype(std::fesetenv(std::forward<TArgs>(args)...))
	{
		return std::fesetenv(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto GetRoundingMode(TArgs&&... args) noexcept(noexcept(std::fegetround(std::forward<TArgs>(args)...)))
	    -> decltype(std::fegetround(std::forward<TArgs>(args)...))
	{
		return std::fegetround(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto HoldFloatExceptions(TArgs&&... args) noexcept(noexcept(std::feholdexcept(std::forward<TArgs>(args)...)))
	    -> decltype(std::feholdexcept(std::forward<TArgs>(args)...))
	{
		return std::feholdexcept(std::forward<TArgs>(args)...);
	}
}
