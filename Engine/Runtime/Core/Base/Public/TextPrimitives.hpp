#pragma once

#include <charconv>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <cstdarg>
#include <string_view>
#include <utility>

namespace rox::core
{
	using DiagnosticText = std::string_view;

	using ToCharsResult = std::to_chars_result;
	using FromCharsResult = std::from_chars_result;
	using ECharsFormat = std::chars_format;

	using WideStringView = std::wstring_view;
	using Utf8StringView = std::u8string_view;

	using VaList = std::va_list;

	template <typename... TArgs>
	constexpr auto ToChars(TArgs&&... args) noexcept(noexcept(std::to_chars(std::forward<TArgs>(args)...)))
	    -> decltype(std::to_chars(std::forward<TArgs>(args)...))
	{
		return std::to_chars(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto FromChars(TArgs&&... args) noexcept(noexcept(std::from_chars(std::forward<TArgs>(args)...)))
	    -> decltype(std::from_chars(std::forward<TArgs>(args)...))
	{
		return std::from_chars(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto FormatText(TArgs&&... args) noexcept(noexcept(std::snprintf(std::forward<TArgs>(args)...)))
	    -> decltype(std::snprintf(std::forward<TArgs>(args)...))
	{
		return std::snprintf(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto FormatWideText(TArgs&&... args) noexcept(noexcept(std::swprintf(std::forward<TArgs>(args)...)))
	    -> decltype(std::swprintf(std::forward<TArgs>(args)...))
	{
		return std::swprintf(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto FormatTextV(TArgs&&... args) noexcept(noexcept(std::vsnprintf(std::forward<TArgs>(args)...)))
	    -> decltype(std::vsnprintf(std::forward<TArgs>(args)...))
	{
		return std::vsnprintf(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto CStringLength(TArgs&&... args) noexcept(noexcept(std::strlen(std::forward<TArgs>(args)...)))
	    -> decltype(std::strlen(std::forward<TArgs>(args)...))
	{
		return std::strlen(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto CompareCString(TArgs&&... args) noexcept(noexcept(std::strcmp(std::forward<TArgs>(args)...)))
	    -> decltype(std::strcmp(std::forward<TArgs>(args)...))
	{
		return std::strcmp(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto FindCStringChar(TArgs&&... args) noexcept(noexcept(std::strchr(std::forward<TArgs>(args)...)))
	    -> decltype(std::strchr(std::forward<TArgs>(args)...))
	{
		return std::strchr(std::forward<TArgs>(args)...);
	}
}
