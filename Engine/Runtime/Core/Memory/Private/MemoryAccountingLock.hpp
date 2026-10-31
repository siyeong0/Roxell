#pragma once

#include <ThreadPrimitives.hpp>
#include <Types.h>
#include <Synchronization.hpp>

namespace rox::core::detail
{
	// Accounting sections contain only counter updates. Never call upstream code while holding this lock.
	// 0 = unlocked, 1 = locked, 2 = locked with a possible parked waiter. This is not a FIFO lock.
	inline void LockMemoryAccounting(Atomic<uint32>& state) noexcept
	{
		uint32 expected = 0;
		if (state.compare_exchange_strong(expected, 1, MemoryOrderAcquire, MemoryOrderRelaxed))
		{
			return;
		}

		constexpr uint32 MAX_YIELD_ATTEMPTS = 64;
		for (uint32 attempt = 0; attempt < MAX_YIELD_ATTEMPTS; ++attempt)
		{
			YieldThread();
			if (state.load(MemoryOrderRelaxed) == 0)
			{
				expected = 0;
				if (state.compare_exchange_weak(expected, 1, MemoryOrderAcquire, MemoryOrderRelaxed))
				{
					return;
				}
			}
		}

		while (state.exchange(2, MemoryOrderAcquire) != 0)
		{
			state.wait(2, MemoryOrderRelaxed);
		}
	}

	inline void UnlockMemoryAccounting(Atomic<uint32>& state) noexcept
	{
		if (state.exchange(0, MemoryOrderRelease) == 2)
		{
			state.notify_one();
		}
	}
}
