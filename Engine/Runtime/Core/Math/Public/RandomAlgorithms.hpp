#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <Types.h>
#include <RandomStream.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace rox::math
{
	// The returned reference borrows one element from the caller's live range.
	template <typename TValue>
	[[nodiscard]] TValue& RandomChoice(RandomStream& stream, TValue* values, core::usize count) noexcept
	{
		ASSERT(values != nullptr && count > 0, "Random choice requires a non-empty valid range.");
		static_assert(sizeof(core::usize) <= sizeof(uint64));
		return values[static_cast<core::usize>(stream.NextU64(0, static_cast<uint64>(count)))];
	}

	// Empty and singleton ranges consume no draws. Swapping must not throw.
	template <typename TValue>
	    requires core::IsNothrowSwappable<TValue>
	void RandomShuffle(RandomStream& stream, TValue* values, core::usize count) noexcept
	{
		ASSERT(values != nullptr || count == 0, "Random shuffle requires a valid range.");
		static_assert(sizeof(core::usize) <= sizeof(uint64));

		for (core::usize remaining = count; remaining > 1; --remaining)
		{
			const core::usize index = static_cast<core::usize>(stream.NextU64(0, static_cast<uint64>(remaining)));
			core::Swap(values[remaining - 1], values[index]);
		}
	}
} // namespace rox::math
