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
	template <typename T, typename TCompare = DefaultLess<RemoveConst<T>>>
	    requires(!IsConst<T> && IsNothrowMoveConstructible<T> &&
	             IsNothrowMoveAssignable<T> && IsNothrowDestructible<T> &&
	             IsNothrowCopyConstructible<TCompare> && IsNothrowCopyAssignable<TCompare> &&
	             IsNothrowDestructible<TCompare> &&
	             IsNothrowInvocableR<bool, const TCompare&, const T&, const T&>)
	void MakeHeap(const Span<T> values, const TCompare& compare = {}) noexcept
	{
		ASSERT(values.Size() <= static_cast<usize>(NumericLimits<isize>::max()), "Heap range must fit iterator distance.");

		if (values.Size() > 1)
		{
			core::MakeHeap(values.begin(), values.end(), core::ConstReference(compare));
		}
	}

	// The range ends with one new value after an already ordered heap prefix.
	template <typename T, typename TCompare = DefaultLess<RemoveConst<T>>>
	    requires(!IsConst<T> && IsNothrowMoveConstructible<T> &&
	             IsNothrowMoveAssignable<T> && IsNothrowDestructible<T> &&
	             IsNothrowCopyConstructible<TCompare> && IsNothrowCopyAssignable<TCompare> &&
	             IsNothrowDestructible<TCompare> &&
	             IsNothrowInvocableR<bool, const TCompare&, const T&, const T&>)
	void PushHeap(const Span<T> values, const TCompare& compare = {}) noexcept
	{
		ASSERT(!values.IsEmpty(), "Heap push requires a non-empty range.");
		ASSERT(values.Size() <= static_cast<usize>(NumericLimits<isize>::max()), "Heap range must fit iterator distance.");
		core::PushHeap(values.begin(), values.end(), core::ConstReference(compare));
	}

	// Moves the top to the last position and repairs the prefix without ending an element lifetime.
	template <typename T, typename TCompare = DefaultLess<RemoveConst<T>>>
	    requires(!IsConst<T> && IsNothrowMoveConstructible<T> &&
	             IsNothrowMoveAssignable<T> && IsNothrowDestructible<T> &&
	             IsNothrowCopyConstructible<TCompare> && IsNothrowCopyAssignable<TCompare> &&
	             IsNothrowDestructible<TCompare> &&
	             IsNothrowInvocableR<bool, const TCompare&, const T&, const T&>)
	void PopHeap(const Span<T> values, const TCompare& compare = {}) noexcept
	{
		ASSERT(!values.IsEmpty(), "Heap pop requires a non-empty range.");
		ASSERT(values.Size() <= static_cast<usize>(NumericLimits<isize>::max()), "Heap range must fit iterator distance.");
		core::PopHeap(values.begin(), values.end(), core::ConstReference(compare));
	}

	template <typename T, typename TCompare = DefaultLess<RemoveConst<T>>>
	    requires(IsNothrowCopyConstructible<TCompare> && IsNothrowCopyAssignable<TCompare> &&
	             IsNothrowDestructible<TCompare> &&
	             IsNothrowInvocableR<bool, const TCompare&, const T&, const T&>)
	[[nodiscard]] bool IsHeap(const Span<T> values, const TCompare& compare = {}) noexcept
	{
		ASSERT(values.Size() <= static_cast<usize>(NumericLimits<isize>::max()), "Heap range must fit iterator distance.");
		return values.Size() < 2 || core::IsHeap(values.begin(), values.end(), core::ConstReference(compare));
	}
}
