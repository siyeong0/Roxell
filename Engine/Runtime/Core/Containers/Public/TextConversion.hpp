#pragma once

#include <cstdlib>
#include <cctype>
#include <clocale>
#include <utility>

namespace rox::core
{
	template <typename... TArgs>
	constexpr auto ParseDouble(TArgs&&... args) noexcept(noexcept(std::strtod(std::forward<TArgs>(args)...)))
	    -> decltype(std::strtod(std::forward<TArgs>(args)...))
	{
		return std::strtod(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto ToLower(TArgs&&... args) noexcept(noexcept(std::tolower(std::forward<TArgs>(args)...)))
	    -> decltype(std::tolower(std::forward<TArgs>(args)...))
	{
		return std::tolower(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto LocaleConvention(TArgs&&... args) noexcept(noexcept(std::localeconv(std::forward<TArgs>(args)...)))
	    -> decltype(std::localeconv(std::forward<TArgs>(args)...))
	{
		return std::localeconv(std::forward<TArgs>(args)...);
	}
}
