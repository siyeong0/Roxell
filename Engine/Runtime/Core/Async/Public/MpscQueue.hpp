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
		explicit MpscQueue(MemoryResource& resource, rox::core::usize capacity) noexcept
		    : m_Queue(resource, capacity)
		{
		}

		MpscQueue(const MpscQueue&) = delete;
		MpscQueue& operator=(const MpscQueue&) = delete;
		MpscQueue(MpscQueue&&) = delete;
		MpscQueue& operator=(MpscQueue&&) = delete;

		rox::core::usize Capacity() const noexcept { return m_Queue.Capacity(); }

		MemoryResource& GetResource() const noexcept { return m_Queue.GetResource(); }

		template <typename... TArguments>
		[[nodiscard]] bool Emplace(TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<TValue, TArguments&&...>
		{
			rox::core::LockGuard lock(m_ProducerMutex);

			return m_Queue.Emplace(rox::core::Forward<TArguments>(arguments)...);
		}

		[[nodiscard]] bool Push(TValue&& value) noexcept
		    requires rox::core::IsNothrowMoveConstructible<TValue>
		{
			return Emplace(rox::core::Move(value));
		}

		[[nodiscard]] bool Push(const TValue& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>
		{
			return Emplace(value);
		}

		[[nodiscard]] bool Pop(TValue* out) noexcept
		    requires rox::core::IsNothrowMoveAssignable<TValue>
		{
			ASSERT(out != nullptr, "Pop requires a valid output pointer.");

			return m_Queue.Pop(out);
		}

	private:
		SpscQueue<TValue> m_Queue;
		rox::core::Mutex m_ProducerMutex;
	};
}
