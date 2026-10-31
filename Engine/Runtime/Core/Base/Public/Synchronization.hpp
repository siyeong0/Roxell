#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>

namespace rox::core
{
	inline constexpr auto DeferLock = std::defer_lock;

	template <typename T> using Atomic = std::atomic<T>;
	using AtomicFlag = std::atomic_flag;

	using Mutex = std::mutex;

	template <typename T> using LockGuard = std::lock_guard<T>;
	template <typename... T> using ScopedLock = std::scoped_lock<T...>;
	template <typename T> using UniqueLock = std::unique_lock<T>;

	using ConditionVariable = std::condition_variable;
	using EWaitStatus = std::cv_status;

	inline constexpr auto TryToLock = std::try_to_lock;

	inline constexpr auto MemoryOrderRelaxed = std::memory_order_relaxed;
	inline constexpr auto MemoryOrderAcquire = std::memory_order_acquire;
	inline constexpr auto MemoryOrderRelease = std::memory_order_release;
	inline constexpr auto MemoryOrderAcqRel = std::memory_order_acq_rel;
	inline constexpr auto MemoryOrderSeqCst = std::memory_order_seq_cst;
}
