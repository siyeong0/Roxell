#pragma once

#include <numeric>
#include <utility>

namespace rox::core
{
	template <typename... TTypes, typename... TArgs>
	constexpr auto Gcd(TArgs&&... args) noexcept(noexcept(std::gcd<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::gcd<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::gcd<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Lcm(TArgs&&... args) noexcept(noexcept(std::lcm<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::lcm<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::lcm<TTypes...>(std::forward<TArgs>(args)...);
	}
}
