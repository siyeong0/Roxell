#pragma once

#include <cstring>
#include <utility>

namespace rox::core
{
	template <typename... TArgs>
	constexpr auto CopyBytes(TArgs&&... args) noexcept(noexcept(std::memcpy(std::forward<TArgs>(args)...)))
	    -> decltype(std::memcpy(std::forward<TArgs>(args)...))
	{
		return std::memcpy(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto MoveBytes(TArgs&&... args) noexcept(noexcept(std::memmove(std::forward<TArgs>(args)...)))
	    -> decltype(std::memmove(std::forward<TArgs>(args)...))
	{
		return std::memmove(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto CompareBytes(TArgs&&... args) noexcept(noexcept(std::memcmp(std::forward<TArgs>(args)...)))
	    -> decltype(std::memcmp(std::forward<TArgs>(args)...))
	{
		return std::memcmp(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto SetBytes(TArgs&&... args) noexcept(noexcept(std::memset(std::forward<TArgs>(args)...)))
	    -> decltype(std::memset(std::forward<TArgs>(args)...))
	{
		return std::memset(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto FindByte(TArgs&&... args) noexcept(noexcept(std::memchr(std::forward<TArgs>(args)...)))
	    -> decltype(std::memchr(std::forward<TArgs>(args)...))
	{
		return std::memchr(std::forward<TArgs>(args)...);
	}
}
