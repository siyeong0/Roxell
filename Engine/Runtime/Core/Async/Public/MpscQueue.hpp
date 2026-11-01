#pragma once

#include <SpscQueue.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <mutex>

namespace rox::core
{
	// Producers serialize admission; the single consumer does not acquire their mutex.
	// Destruction requires every participant to be quiescent. Payloads must not reenter this queue.
	template <typename TValue> 
	class MpscQueue final
	{
	public:
		explicit MpscQueue(MemoryResource& resource, usize capacity) noexcept
		    : m_Queue(resource, capacity)
		{
		}

		MpscQueue(const MpscQueue&) = delete;
		MpscQueue& operator=(const MpscQueue&) = delete;
		MpscQueue(MpscQueue&&) = delete;
		MpscQueue& operator=(MpscQueue&&) = delete;

		usize Capacity() const noexcept { return m_Queue.Capacity(); }

		MemoryResource& GetResource() const noexcept { return m_Queue.GetResource(); }

		template <typename... TArguments>
		[[nodiscard]] bool Emplace(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments&&...>
		{
			core::LockGuard lock(m_ProducerMutex);

			return m_Queue.Emplace(core::Forward<TArguments>(arguments)...);
		}

		[[nodiscard]] bool Push(TValue&& value) noexcept
		    requires IsNothrowMoveConstructible<TValue>
		{
			return Emplace(core::Move(value));
		}

		[[nodiscard]] bool Push(const TValue& value) noexcept
		    requires IsNothrowCopyConstructible<TValue>
		{
			return Emplace(value);
		}

		[[nodiscard]] bool Pop(TValue* out) noexcept
		    requires IsNothrowMoveAssignable<TValue>
		{
			ASSERT(out != nullptr, "Pop requires a valid output pointer.");

			return m_Queue.Pop(out);
		}

	private:
		SpscQueue<TValue> m_Queue;
		core::Mutex m_ProducerMutex;
	};
}
