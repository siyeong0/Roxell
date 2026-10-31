#pragma once

#include <MemoryBlock.h>
#include <Synchronization.hpp>
#include <Types.h>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <atomic>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// One producer and one consumer may operate concurrently. Destruction requires both to be quiescent.
	template <typename TValue> 
	class SpscQueue final
	{
		static_assert(rox::core::IsNothrowDestructible<TValue>);

	public:
		explicit SpscQueue(MemoryResource& resource, rox::core::usize capacity) noexcept
		    : m_Storage(MemoryBlock::CreateRequired(resource, storageSize(capacity)))
		    , m_SlotCount(capacity + 1)
		{
		}

		SpscQueue(const SpscQueue&) = delete;
		SpscQueue& operator=(const SpscQueue&) = delete;
		SpscQueue(SpscQueue&&) = delete;
		SpscQueue& operator=(SpscQueue&&) = delete;

		~SpscQueue() noexcept
		{
			usize head = m_Head.load(rox::core::MemoryOrderRelaxed);
			const usize tail = m_Tail.load(rox::core::MemoryOrderRelaxed);

			while (head != tail)
			{
				rox::core::DestroyAt(data() + head);
				head = next(head);
			}
		}

		rox::core::usize Capacity() const noexcept { return m_SlotCount - 1; }

		MemoryResource& GetResource() const noexcept { return *m_Storage.GetResource(); }

		// Full admission leaves arguments untouched. Payload operations must not reenter this queue.
		template <typename... TArguments>
		[[nodiscard]] bool Emplace(TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<TValue, TArguments&&...>
		{
			const usize tail = m_Tail.load(rox::core::MemoryOrderRelaxed);
			const usize following = next(tail);

			if (following == m_Head.load(rox::core::MemoryOrderAcquire))
			{
				return false;
			}

			rox::core::ConstructAt(data() + tail, rox::core::Forward<TArguments>(arguments)...);
			m_Tail.store(following, rox::core::MemoryOrderRelease);

			return true;
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

			const usize head = m_Head.load(rox::core::MemoryOrderRelaxed);

			if (head == m_Tail.load(rox::core::MemoryOrderAcquire))
			{
				return false;
			}

			(*out) = rox::core::Move(data()[head]);
			rox::core::DestroyAt(data() + head);
			m_Head.store(next(head), rox::core::MemoryOrderRelease);

			return true;
		}

	private:
		static MemoryAllocationRequest storageSize(rox::core::usize capacity) noexcept
		{
			ASSERT(capacity > 0 && capacity < rox::core::NumericLimits<rox::core::usize>::max() / sizeof(TValue),
			       "SpscQueue capacity requires positive representable slot storage.");

			return {(capacity + 1) * sizeof(TValue), alignof(TValue)};
		}

		TValue* data() noexcept { return static_cast<TValue*>(m_Storage.GetData()); }

		rox::core::usize next(rox::core::usize index) const noexcept { return index + 1 == m_SlotCount ? 0 : index + 1; }

	private:
		MemoryBlock m_Storage;
		const rox::core::usize m_SlotCount;
		rox::core::Atomic<rox::core::usize> m_Head = 0;
		rox::core::Atomic<rox::core::usize> m_Tail = 0;
	};
}
