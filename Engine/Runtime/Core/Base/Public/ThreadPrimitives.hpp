#pragma once

#include <thread>
#include <utility>

namespace rox::core
{
	using ExecutionThreadId = std::thread::id;

	template <typename... TArgs>
	constexpr auto CurrentExecutionThreadId(TArgs&&... args) noexcept(noexcept(std::this_thread::get_id(std::forward<TArgs>(args)...)))
	    -> decltype(std::this_thread::get_id(std::forward<TArgs>(args)...))
	{
		return std::this_thread::get_id(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto SleepFor(TArgs&&... args) noexcept(noexcept(std::this_thread::sleep_for(std::forward<TArgs>(args)...)))
	    -> decltype(std::this_thread::sleep_for(std::forward<TArgs>(args)...))
	{
		return std::this_thread::sleep_for(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto YieldThread(TArgs&&... args) noexcept(noexcept(std::this_thread::yield(std::forward<TArgs>(args)...)))
	    -> decltype(std::this_thread::yield(std::forward<TArgs>(args)...))
	{
		return std::this_thread::yield(std::forward<TArgs>(args)...);
	}
}
