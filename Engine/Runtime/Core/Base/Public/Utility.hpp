#pragma once

#include <utility>
#include <functional>
#include <bit>
#include <cstddef>

namespace rox::core
{
	template <typename... TTypes, typename... TArgs>
	constexpr auto Move(TArgs&&... args) noexcept(noexcept(std::move<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::move<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::move<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Forward(TArgs&&... args) noexcept(noexcept(std::forward<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::forward<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::forward<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename T, typename U = T>
	constexpr T Exchange(T& value, U&& replacement)
	    noexcept(noexcept(std::exchange(value, std::forward<U>(replacement))))
	{
		return std::exchange(value, std::forward<U>(replacement));
	}


	using std::swap;
	template <typename T> constexpr void SwapValue(T& left, T& right) noexcept(noexcept(swap(left, right)))
	{
		swap(left, right);
	}

	template <typename T> constexpr void Swap(T& left, T& right) noexcept(noexcept(SwapValue(left, right)))
	{
		SwapValue(left, right);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto AsConst(TArgs&&... args) noexcept(noexcept(std::as_const<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::as_const<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::as_const<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto AddressOf(TArgs&&... args) noexcept(noexcept(std::addressof<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::addressof<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::addressof<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Invoke(TArgs&&... args) noexcept(noexcept(std::invoke<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::invoke<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::invoke<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto ConstReference(TArgs&&... args) noexcept(noexcept(std::cref<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::cref<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::cref<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto BitCast(TArgs&&... args) noexcept(noexcept(std::bit_cast<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::bit_cast<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::bit_cast<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto ToInteger(TArgs&&... args) noexcept(noexcept(std::to_integer<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::to_integer<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::to_integer<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto RotateRight(TArgs&&... args) noexcept(noexcept(std::rotr<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::rotr<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::rotr<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto BitWidth(TArgs&&... args) noexcept(noexcept(std::bit_width<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::bit_width<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::bit_width<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto CountTrailingZeros(TArgs&&... args) noexcept(noexcept(std::countr_zero<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::countr_zero<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::countr_zero<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto PopCount(TArgs&&... args) noexcept(noexcept(std::popcount<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::popcount<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::popcount<TTypes...>(std::forward<TArgs>(args)...);
	}

}
