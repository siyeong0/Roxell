#pragma once

#include <new>
#include <exception>
#include <system_error>
#include <utility>

namespace rox::core
{
	using AllocationException = std::bad_alloc;
	using Exception = std::exception;
	using SystemException = std::system_error;

	using SystemErrorCode = std::error_code;
	using EErrorCondition = std::errc;

	template <typename... TArgs>
	constexpr auto SystemCategory(TArgs&&... args) noexcept(noexcept(std::system_category(std::forward<TArgs>(args)...)))
	    -> decltype(std::system_category(std::forward<TArgs>(args)...))
	{
		return std::system_category(std::forward<TArgs>(args)...);
	}
}
