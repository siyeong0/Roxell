#pragma once

#include <Assertion.h>
#include <Algorithms.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <Ordering.hpp>
#include <Span.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <type_traits>

namespace rox::core
{
	// Borrows live contiguous values; default ordering puts the largest value first.
	template <typename T, typename TCompare = DefaultLess<rox::core::RemoveConst<T>>>
	    requires(!rox::core::IsConst<T> && rox::core::IsNothrowMoveConstructible<T> &&
	             rox::core::IsNothrowMoveAssignable<T> && rox::core::IsNothrowDestructible<T> &&
	             rox::core::IsNothrowCopyConstructible<TCompare> && rox::core::IsNothrowCopyAssignable<TCompare> &&
	             rox::core::IsNothrowDestructible<TCompare> &&
	             rox::core::IsNothrowInvocableR<bool, const TCompare&, const T&, const T&>)
	void MakeHeap(const Span<T> values, const TCompare& compare = {}) noexcept
	{
		ASSERT(values.Size() <= static_cast<rox::core::usize>(rox::core::NumericLimits<rox::core::isize>::max()), "Heap range must fit iterator distance.");

		if (values.Size() > 1)
		{
			rox::core::MakeHeap(values.begin(), values.end(), rox::core::ConstReference(compare));
		}
	}

	// The range ends with one new value after an already ordered heap prefix.
	template <typename T, typename TCompare = DefaultLess<rox::core::RemoveConst<T>>>
	    requires(!rox::core::IsConst<T> && rox::core::IsNothrowMoveConstructible<T> &&
	             rox::core::IsNothrowMoveAssignable<T> && rox::core::IsNothrowDestructible<T> &&
	             rox::core::IsNothrowCopyConstructible<TCompare> && rox::core::IsNothrowCopyAssignable<TCompare> &&
	             rox::core::IsNothrowDestructible<TCompare> &&
	             rox::core::IsNothrowInvocableR<bool, const TCompare&, const T&, const T&>)
	void PushHeap(const Span<T> values, const TCompare& compare = {}) noexcept
	{
		ASSERT(!values.IsEmpty(), "Heap push requires a non-empty range.");
		ASSERT(values.Size() <= static_cast<rox::core::usize>(rox::core::NumericLimits<rox::core::isize>::max()), "Heap range must fit iterator distance.");
		rox::core::PushHeap(values.begin(), values.end(), rox::core::ConstReference(compare));
	}

	// Moves the top to the last position and repairs the prefix without ending an element lifetime.
	template <typename T, typename TCompare = DefaultLess<rox::core::RemoveConst<T>>>
	    requires(!rox::core::IsConst<T> && rox::core::IsNothrowMoveConstructible<T> &&
	             rox::core::IsNothrowMoveAssignable<T> && rox::core::IsNothrowDestructible<T> &&
	             rox::core::IsNothrowCopyConstructible<TCompare> && rox::core::IsNothrowCopyAssignable<TCompare> &&
	             rox::core::IsNothrowDestructible<TCompare> &&
	             rox::core::IsNothrowInvocableR<bool, const TCompare&, const T&, const T&>)
	void PopHeap(const Span<T> values, const TCompare& compare = {}) noexcept
	{
		ASSERT(!values.IsEmpty(), "Heap pop requires a non-empty range.");
		ASSERT(values.Size() <= static_cast<rox::core::usize>(rox::core::NumericLimits<rox::core::isize>::max()), "Heap range must fit iterator distance.");
		rox::core::PopHeap(values.begin(), values.end(), rox::core::ConstReference(compare));
	}

	template <typename T, typename TCompare = DefaultLess<rox::core::RemoveConst<T>>>
	    requires(rox::core::IsNothrowCopyConstructible<TCompare> && rox::core::IsNothrowCopyAssignable<TCompare> &&
	             rox::core::IsNothrowDestructible<TCompare> &&
	             rox::core::IsNothrowInvocableR<bool, const TCompare&, const T&, const T&>)
	[[nodiscard]] bool IsHeap(const Span<T> values, const TCompare& compare = {}) noexcept
	{
		ASSERT(values.Size() <= static_cast<rox::core::usize>(rox::core::NumericLimits<rox::core::isize>::max()), "Heap range must fit iterator distance.");
		return values.Size() < 2 || rox::core::IsHeap(values.begin(), values.end(), rox::core::ConstReference(compare));
	}
}
