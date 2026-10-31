#pragma once

#include <Assertion.h>
#include <ByteOperations.hpp>
#include <Types.h>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <MemoryResource.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// The resource outlives storage. Mutation is exclusive; constructors/destructors cannot re-enter the queue.
	template <typename TValue> 
	class RingBuffer final
	{
		static_assert(rox::core::IsNothrowMoveConstructible<TValue>);
		static_assert(rox::core::IsNothrowDestructible<TValue>);

	public:
		[[nodiscard]] TValue& At(rox::core::usize index) noexcept;
		[[nodiscard]] const TValue& At(rox::core::usize index) const noexcept;

		explicit RingBuffer(MemoryResource& resource, const rox::core::usize capacity = 0) noexcept;

		RingBuffer(const RingBuffer& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		RingBuffer(RingBuffer&& other) noexcept;

		~RingBuffer() noexcept;

		// Assignment keeps the destination resource and invalidates its references; moving invalidates source references.
		RingBuffer& operator=(const RingBuffer& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		RingBuffer& operator=(RingBuffer&& other) noexcept;

		[[nodiscard]] rox::core::usize Size() const noexcept;
		[[nodiscard]] rox::core::usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;
		[[nodiscard]] bool IsFull() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		// Requires zero capacity (including after a move). Failure is retryable; no live values are replaced.
		[[nodiscard]] bool Initialize(const rox::core::usize capacity) noexcept;

		[[nodiscard]] TValue& Front() noexcept;
		[[nodiscard]] const TValue& Front() const noexcept;

		[[nodiscard]] TValue& Back() noexcept;
		[[nodiscard]] const TValue& Back() const noexcept;

		[[nodiscard]] TValue& operator[](const rox::core::usize logicalIndex) noexcept;
		[[nodiscard]] const TValue& operator[](const rox::core::usize logicalIndex) const noexcept;

		// Live slot addresses survive unrelated pushes and pops; full admission never overwrites an element.
		template <typename... TArguments>
		TValue& EmplaceRequired(TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<TValue, TArguments&&...>;

		TValue& PushRequired(const TValue& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		TValue& PushRequired(TValue&& value) noexcept;

		// Saturation is an ordinary refusal; supplied arguments are not consumed on that path.
		template <typename... TArguments>
		[[nodiscard]] TValue* Emplace(TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<TValue, TArguments&&...>;

		[[nodiscard]] bool Push(const TValue& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		[[nodiscard]] bool Push(TValue&& value) noexcept;

		void Pop() noexcept;

		void Clear() noexcept;

		// Success replaces output with the same capacity and FIFO values; failure preserves output.
		[[nodiscard]] bool CloneTo(MemoryResource& resource, RingBuffer* outQueue) const noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

	private:
		[[nodiscard]] rox::core::usize physicalIndex(const rox::core::usize logicalIndex) const noexcept;

		void copyFrom(const RingBuffer& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		void releaseStorage() noexcept;
		void stealStorage(RingBuffer& other) noexcept;

	private:
		TValue* m_Data = nullptr;
		rox::core::usize m_Size = 0;
		rox::core::usize m_Capacity = 0;
		rox::core::usize m_Head = 0;
		MemoryResource* m_Resource;
	};
}

namespace rox::core
{
	template <typename TValue>
	TValue& RingBuffer<TValue>::At(rox::core::usize index) noexcept
	{
		return (*this)[index];
	}

	template <typename TValue>
	const TValue& RingBuffer<TValue>::At(rox::core::usize index) const noexcept
	{
		return (*this)[index];
	}

	template <typename TValue>
	RingBuffer<TValue>::RingBuffer(MemoryResource& resource, const rox::core::usize capacity) noexcept
	    : m_Capacity(capacity)
	    , m_Resource(&resource)
	{
		ASSERT(capacity <= rox::core::NumericLimits<rox::core::usize>::max() / sizeof(TValue),
		       "RingBuffer capacity must fit its representable allocation size.");

		if (capacity != 0)
		{
			m_Data = static_cast<TValue*>(resource.AllocateRequired({capacity * sizeof(TValue), alignof(TValue)}));
		}
	}

	template <typename TValue>
	RingBuffer<TValue>::RingBuffer(const RingBuffer& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	    : RingBuffer(*other.m_Resource, other.m_Capacity)
	{
		copyFrom(other);
	}

	template <typename TValue>
	RingBuffer<TValue>::RingBuffer(RingBuffer&& other) noexcept
	    : m_Resource(other.m_Resource)
	{
		stealStorage(other);
	}

	template <typename TValue>
	RingBuffer<TValue>::~RingBuffer() noexcept
	{
		releaseStorage();
	}

	template <typename TValue>
	RingBuffer<TValue>& RingBuffer<TValue>::operator=(const RingBuffer& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		if (this != &other)
		{
			RingBuffer replacement(*m_Resource, other.m_Capacity);
			replacement.copyFrom(other);
			releaseStorage();
			stealStorage(replacement);
		}

		return *this;
	}

	template <typename TValue>
	RingBuffer<TValue>& RingBuffer<TValue>::operator=(RingBuffer&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		if (m_Resource == other.m_Resource)
		{
			releaseStorage();
			stealStorage(other);
			return *this;
		}
		RingBuffer replacement(*m_Resource, other.m_Capacity);

		for (rox::core::usize index = 0; index < other.m_Size; ++index)
		{
			replacement.EmplaceRequired(rox::core::Move(other[index]));
		}
		releaseStorage();
		stealStorage(replacement);
		other.releaseStorage();
		return *this;
	}

	template <typename TValue>
	rox::core::usize RingBuffer<TValue>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename TValue>
	rox::core::usize RingBuffer<TValue>::Capacity() const noexcept
	{
		return m_Capacity;
	}

	template <typename TValue>
	bool RingBuffer<TValue>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename TValue>
	bool RingBuffer<TValue>::IsFull() const noexcept
	{
		return m_Size == m_Capacity;
	}

	template <typename TValue>
	MemoryResource& RingBuffer<TValue>::GetResource() const noexcept
	{
		return *m_Resource;
	}

	template <typename TValue>
	bool RingBuffer<TValue>::Initialize(const rox::core::usize capacity) noexcept
	{
		ASSERT(m_Capacity == 0, "RingBuffer::Initialize requires zero capacity.");
		if (capacity > rox::core::NumericLimits<rox::core::usize>::max() / sizeof(TValue))
		{
			return false;
		}
		TValue* data = static_cast<TValue*>(m_Resource->Allocate({capacity * sizeof(TValue), alignof(TValue)}));
		if (data == nullptr && capacity != 0)
		{
			return false;
		}
		m_Data = data;
		m_Capacity = capacity;
		return true;
	}

	template <typename TValue>
	TValue& RingBuffer<TValue>::Front() noexcept
	{
		ASSERT(m_Size != 0, "RingBuffer::Front requires a live element.");
		return m_Data[m_Head];
	}

	template <typename TValue>
	const TValue& RingBuffer<TValue>::Front() const noexcept
	{
		ASSERT(m_Size != 0, "RingBuffer::Front requires a live element.");
		return m_Data[m_Head];
	}

	template <typename TValue>
	TValue& RingBuffer<TValue>::Back() noexcept
	{
		ASSERT(m_Size != 0, "RingBuffer::Back requires a live element.");
		return m_Data[physicalIndex(m_Size - 1)];
	}

	template <typename TValue>
	const TValue& RingBuffer<TValue>::Back() const noexcept
	{
		ASSERT(m_Size != 0, "RingBuffer::Back requires a live element.");
		return m_Data[physicalIndex(m_Size - 1)];
	}

	template <typename TValue>
	TValue& RingBuffer<TValue>::operator[](const rox::core::usize logicalIndex) noexcept
	{
		ASSERT(logicalIndex < m_Size, "RingBuffer index must address a live element.");
		return m_Data[physicalIndex(logicalIndex)];
	}

	template <typename TValue>
	const TValue& RingBuffer<TValue>::operator[](const rox::core::usize logicalIndex) const noexcept
	{
		ASSERT(logicalIndex < m_Size, "RingBuffer index must address a live element.");
		return m_Data[physicalIndex(logicalIndex)];
	}

	template <typename TValue>
	template <typename... TArguments>
	TValue& RingBuffer<TValue>::EmplaceRequired(TArguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<TValue, TArguments&&...>
	{
		ASSERT(m_Size < m_Capacity, "RingBuffer::EmplaceRequired requires available capacity.");
		TValue* const destination = m_Data + physicalIndex(m_Size);
		rox::core::ConstructAt(destination, rox::core::Forward<TArguments>(arguments)...);
		++m_Size;
		return *destination;
	}

	template <typename TValue>
	TValue& RingBuffer<TValue>::PushRequired(const TValue& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		return EmplaceRequired(value);
	}

	template <typename TValue>
	TValue& RingBuffer<TValue>::PushRequired(TValue&& value) noexcept
	{
		return EmplaceRequired(rox::core::Move(value));
	}

	template <typename TValue>
	template <typename... TArguments>
	TValue* RingBuffer<TValue>::Emplace(TArguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<TValue, TArguments&&...>
	{
		if (IsFull())
		{
			return nullptr;
		}

		return rox::core::AddressOf(EmplaceRequired(rox::core::Forward<TArguments>(arguments)...));
	}

	template <typename TValue>
	bool RingBuffer<TValue>::Push(const TValue& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		return Emplace(value) != nullptr;
	}

	template <typename TValue>
	bool RingBuffer<TValue>::Push(TValue&& value) noexcept
	{
		return Emplace(rox::core::Move(value)) != nullptr;
	}

	template <typename TValue>
	void RingBuffer<TValue>::Pop() noexcept
	{
		ASSERT(m_Size != 0, "RingBuffer::Pop requires a live element.");
		rox::core::DestroyAt(m_Data + m_Head);
		--m_Size;
		++m_Head;

		if (m_Head == m_Capacity || m_Size == 0)
		{
			m_Head = 0;
		}
	}

	template <typename TValue>
	void RingBuffer<TValue>::Clear() noexcept
	{
		if constexpr (rox::core::IsTriviallyDestructible<TValue>)
		{
			m_Size = 0;
			m_Head = 0;
		}
		else
		{
			while (m_Size != 0)
			{
				Pop();
			}
		}
	}

	template <typename TValue>
	bool RingBuffer<TValue>::CloneTo(MemoryResource& resource, RingBuffer* outQueue) const noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		ASSERT(outQueue != nullptr && &outQueue->GetResource() == &resource, "RingBuffer::CloneTo output must use the requested resource.");
		RingBuffer replacement(resource);
		if (!replacement.Initialize(m_Capacity))
		{
			return false;
		}
		replacement.copyFrom(*this);
		*outQueue = rox::core::Move(replacement);
		return true;
	}

	template <typename TValue>
	rox::core::usize RingBuffer<TValue>::physicalIndex(const rox::core::usize logicalIndex) const noexcept
	{
		const rox::core::usize remainingSlots = m_Capacity - m_Head;
		return logicalIndex < remainingSlots ? m_Head + logicalIndex : logicalIndex - remainingSlots;
	}

	template <typename TValue>
	void RingBuffer<TValue>::copyFrom(const RingBuffer& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		if constexpr (rox::core::IsTriviallyCopyable<TValue>)
		{
			if (other.m_Size != 0)
			{
				const rox::core::usize contiguousCount = other.m_Capacity - other.m_Head;
				const rox::core::usize firstCount =
				    other.m_Size < contiguousCount ? other.m_Size : contiguousCount;
				rox::core::CopyBytes(m_Data, other.m_Data + other.m_Head, firstCount * sizeof(TValue));
				if (firstCount != other.m_Size)
				{
					rox::core::CopyBytes(m_Data + firstCount, other.m_Data,
					                     (other.m_Size - firstCount) * sizeof(TValue));
				}
			}
			m_Size = other.m_Size;
		}
		else
		{
			for (rox::core::usize index = 0; index < other.m_Size; ++index)
			{
				EmplaceRequired(other[index]);
			}
		}
	}

	template <typename TValue>
	void RingBuffer<TValue>::releaseStorage() noexcept
	{
		Clear();

		if (m_Data != nullptr)
		{
			m_Resource->Deallocate(m_Data, {m_Capacity * sizeof(TValue), alignof(TValue)});
		}

		m_Data = nullptr;
		m_Capacity = 0;
	}

	template <typename TValue>
	void RingBuffer<TValue>::stealStorage(RingBuffer& other) noexcept
	{
		m_Data = rox::core::Exchange(other.m_Data, nullptr);
		m_Size = rox::core::Exchange(other.m_Size, 0);
		m_Capacity = rox::core::Exchange(other.m_Capacity, 0);
		m_Head = rox::core::Exchange(other.m_Head, 0);
	}
}
