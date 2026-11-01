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
		static_assert(IsNothrowDestructible<TValue>);

	public:
		explicit SpscQueue(MemoryResource& resource, usize capacity) noexcept
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
			usize head = m_Head.load(core::MemoryOrderRelaxed);
			const usize tail = m_Tail.load(core::MemoryOrderRelaxed);

			while (head != tail)
			{
				core::DestroyAt(data() + head);
				head = next(head);
			}
		}

		usize Capacity() const noexcept { return m_SlotCount - 1; }

		MemoryResource& GetResource() const noexcept { return *m_Storage.GetResource(); }

		// Full admission leaves arguments untouched. Payload operations must not reenter this queue.
		template <typename... TArguments>
		[[nodiscard]] bool Emplace(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments&&...>
		{
			const usize tail = m_Tail.load(core::MemoryOrderRelaxed);
			const usize following = next(tail);

			if (following == m_Head.load(core::MemoryOrderAcquire))
			{
				return false;
			}

			core::ConstructAt(data() + tail, core::Forward<TArguments>(arguments)...);
			m_Tail.store(following, core::MemoryOrderRelease);

			return true;
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

			const usize head = m_Head.load(core::MemoryOrderRelaxed);

			if (head == m_Tail.load(core::MemoryOrderAcquire))
			{
				return false;
			}

			(*out) = core::Move(data()[head]);
			core::DestroyAt(data() + head);
			m_Head.store(next(head), core::MemoryOrderRelease);

			return true;
		}

	private:
		static MemoryAllocationRequest storageSize(usize capacity) noexcept
		{
			ASSERT(capacity > 0 && capacity < NumericLimits<usize>::max() / sizeof(TValue),
			       "SpscQueue capacity requires positive representable slot storage.");

			return {(capacity + 1) * sizeof(TValue), alignof(TValue)};
		}

		TValue* data() noexcept { return static_cast<TValue*>(m_Storage.GetData()); }

		usize next(usize index) const noexcept { return index + 1 == m_SlotCount ? 0 : index + 1; }

	private:
		MemoryBlock m_Storage;
		const usize m_SlotCount;
		Atomic<usize> m_Head = 0;
		Atomic<usize> m_Tail = 0;
	};
}
