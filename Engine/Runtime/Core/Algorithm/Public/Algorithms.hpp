#pragma once

#include <algorithm>
#include <iterator>
#include <utility>

namespace rox::core
{
	template <typename... TTypes, typename... TArgs>
	constexpr auto Sort(TArgs&&... args) noexcept(noexcept(std::sort<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::sort<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::sort<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto LowerBound(TArgs&&... args) noexcept(noexcept(std::lower_bound<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::lower_bound<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::lower_bound<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto UpperBound(TArgs&&... args) noexcept(noexcept(std::upper_bound<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::upper_bound<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::upper_bound<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto BinarySearch(TArgs&&... args) noexcept(noexcept(std::binary_search<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::binary_search<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::binary_search<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Copy(TArgs&&... args) noexcept(noexcept(std::copy<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::copy<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::copy<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto CopyN(TArgs&&... args) noexcept(noexcept(std::copy_n<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::copy_n<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::copy_n<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Find(TArgs&&... args) noexcept(noexcept(std::find<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::find<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::find<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto FindIf(TArgs&&... args) noexcept(noexcept(std::find_if<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::find_if<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::find_if<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Fill(TArgs&&... args) noexcept(noexcept(std::fill<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::fill<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::fill<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto FillN(TArgs&&... args) noexcept(noexcept(std::fill_n<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::fill_n<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::fill_n<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Equal(TArgs&&... args) noexcept(noexcept(std::equal<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::equal<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::equal<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto AnyOf(TArgs&&... args) noexcept(noexcept(std::any_of<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::any_of<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::any_of<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto AllOf(TArgs&&... args) noexcept(noexcept(std::all_of<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::all_of<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::all_of<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto NoneOf(TArgs&&... args) noexcept(noexcept(std::none_of<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::none_of<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::none_of<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto LexicographicalCompare(TArgs&&... args) noexcept(noexcept(std::lexicographical_compare<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::lexicographical_compare<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::lexicographical_compare<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto IsSorted(TArgs&&... args) noexcept(noexcept(std::is_sorted<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::is_sorted<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::is_sorted<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto AdjacentFind(TArgs&&... args) noexcept(noexcept(std::adjacent_find<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::adjacent_find<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::adjacent_find<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto PopHeap(TArgs&&... args) noexcept(noexcept(std::pop_heap<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::pop_heap<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::pop_heap<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto PushHeap(TArgs&&... args) noexcept(noexcept(std::push_heap<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::push_heap<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::push_heap<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto MakeHeap(TArgs&&... args) noexcept(noexcept(std::make_heap<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::make_heap<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::make_heap<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto IsHeap(TArgs&&... args) noexcept(noexcept(std::is_heap<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::is_heap<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::is_heap<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Rotate(TArgs&&... args) noexcept(noexcept(std::rotate<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::rotate<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::rotate<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Reverse(TArgs&&... args) noexcept(noexcept(std::reverse<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::reverse<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::reverse<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Replace(TArgs&&... args) noexcept(noexcept(std::replace<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::replace<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::replace<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto NthElement(TArgs&&... args) noexcept(noexcept(std::nth_element<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::nth_element<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::nth_element<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Begin(TArgs&&... args) noexcept(noexcept(std::begin<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::begin<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::begin<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto End(TArgs&&... args) noexcept(noexcept(std::end<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::end<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::end<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Size(TArgs&&... args) noexcept(noexcept(std::size<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::size<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::size<TTypes...>(std::forward<TArgs>(args)...);
	}
}
