#pragma once

#include <Assertion.h>
#include <CompilerAttributes.h>

#include <memory>
#include <new>
#include <utility>

namespace rox::core
{
	// Specializations construct one destination value and consume the source without releasing
	// transferred ownership. A retired source must not be accessed or destroyed; its lifetime may
	// end on subsequent same-type storage reuse or release. The destination is uninitialized.
	// A trait alone cannot make byte relocation of an arbitrary C++20 class well-defined.
	template <typename T>
	struct Relocation
	{
		static constexpr bool IsCustomized = false;
		static void IntoUninitialized(T* destination, T* source) noexcept
		{
			static_assert(std::is_nothrow_move_constructible_v<T>);
			static_assert(std::is_nothrow_destructible_v<T>);
			std::construct_at(destination, std::move(*source));
			std::destroy_at(source);
		}
	};

	namespace detail
	{
		// Container bounds and raw-slot ownership already establish these preconditions.
		template <typename T>
		ROXELL_FORCE_INLINE void RelocateAtUnchecked(T* destination, T* source) noexcept
		{
			static_assert(noexcept(Relocation<T>::IntoUninitialized(destination, source)));
			Relocation<T>::IntoUninitialized(destination, source);
		}
	}

	// Only raw container slots may be consumed; do not relocate an automatic variable whose
	// destructor will still run. Overlapping ranges require callers to choose the safe direction.
	template <typename T>
	void RelocateAt(T* destination, T* source) noexcept
	{
		ASSERT(destination != nullptr && source != nullptr && destination != source, "Relocation requires distinct valid destination and source slots.");
		detail::RelocateAtUnchecked(destination, source);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto ConstructAt(TArgs&&... args) noexcept(noexcept(std::construct_at<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::construct_at<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::construct_at<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto DestroyAt(TArgs&&... args) noexcept(noexcept(std::destroy_at<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::destroy_at<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::destroy_at<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto DestroyN(TArgs&&... args) noexcept(noexcept(std::destroy_n<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::destroy_n<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::destroy_n<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto ValueConstructN(TArgs&&... args) noexcept(noexcept(std::uninitialized_value_construct_n<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::uninitialized_value_construct_n<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::uninitialized_value_construct_n<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto UninitializedCopyN(TArgs&&... args) noexcept(noexcept(std::uninitialized_copy_n<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::uninitialized_copy_n<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::uninitialized_copy_n<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto UninitializedFillN(TArgs&&... args) noexcept(noexcept(std::uninitialized_fill_n<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::uninitialized_fill_n<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::uninitialized_fill_n<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Launder(TArgs&&... args) noexcept(noexcept(std::launder<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::launder<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::launder<TTypes...>(std::forward<TArgs>(args)...);
	}
}
