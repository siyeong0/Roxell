#pragma once

#include <Array.hpp>
#include <Types.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <BinaryHeap.hpp>
#include <MemoryResource.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// The resource and borrowed comparator state outlive the queue. Mutation requires exclusive access.
	template <typename T, typename TCompare = DefaultLess<T>> 
	class PriorityQueue
	{
		static_assert(rox::core::IsNothrowMoveConstructible<T> && rox::core::IsNothrowMoveAssignable<T>);
		static_assert(rox::core::IsNothrowDestructible<T>);
		static_assert(rox::core::IsNothrowCopyConstructible<TCompare> && rox::core::IsNothrowCopyAssignable<TCompare>);
		static_assert(rox::core::IsNothrowDestructible<TCompare>);
		static_assert(rox::core::IsNothrowInvocableR<bool, const TCompare&, const T&, const T&>);

	public:
		explicit PriorityQueue(MemoryResource& resource, const TCompare& compare = {}) noexcept;

		PriorityQueue(const PriorityQueue& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

		PriorityQueue(PriorityQueue&& other) noexcept;

		PriorityQueue& operator=(const PriorityQueue& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

		PriorityQueue& operator=(PriorityQueue&& other) noexcept;

		~PriorityQueue() noexcept = default;

		[[nodiscard]] rox::core::usize Size() const noexcept;
		[[nodiscard]] rox::core::usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		[[nodiscard]] const T& Top() const noexcept;

		void ReserveRequired(rox::core::usize capacity) noexcept;
		[[nodiscard]] bool Reserve(const rox::core::usize capacity) noexcept;

		void Clear() noexcept;
		void Reset() noexcept;

		void PushRequired(const T& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

		void PushRequired(T&& value) noexcept;

		[[nodiscard]] bool Push(const T& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

		[[nodiscard]] bool Push(T&& value) noexcept;

		// Refused growth preserves the heap and does not consume aliased arguments.
		template <typename... TArguments>
		[[nodiscard]] bool Emplace(TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<T, TArguments&&...>;

		// Appending consumes aliased arguments before growth; repairing the heap may move every logical item.
		template <typename... TArguments>
		void EmplaceRequired(TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<T, TArguments&&...>;

		void Pop() noexcept;

		[[nodiscard]] T ExtractTop() noexcept;

		// Input may borrow the current Top; keep its storage alive until the replacement is complete.
		void AssignRequired(const Span<const T> values) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

		[[nodiscard]] bool Assign(const Span<const T> values) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

	private:
		Array<T> m_Values;
		TCompare m_Compare;
	};
}

namespace rox::core
{
	template <typename T, typename TCompare>
	PriorityQueue<T, TCompare>::PriorityQueue(MemoryResource& resource, const TCompare& compare) noexcept
	    : m_Values(resource)
	    , m_Compare(compare)
	{
	}

	template <typename T, typename TCompare>
	PriorityQueue<T, TCompare>::PriorityQueue(const PriorityQueue& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	    : m_Values(other.m_Values)
	    , m_Compare(other.m_Compare)
	{
	}

	template <typename T, typename TCompare>
	PriorityQueue<T, TCompare>::PriorityQueue(PriorityQueue&& other) noexcept
	    : m_Values(rox::core::Move(other.m_Values))
	    , m_Compare(other.m_Compare)
	{
	}

	template <typename T, typename TCompare>
	PriorityQueue<T, TCompare>& PriorityQueue<T, TCompare>::operator=(const PriorityQueue& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	{
		if (this != &other)
		{
			m_Values = other.m_Values;
			m_Compare = other.m_Compare;
		}

		return *this;
	}

	template <typename T, typename TCompare>
	PriorityQueue<T, TCompare>& PriorityQueue<T, TCompare>::operator=(PriorityQueue&& other) noexcept
	{
		if (this != &other)
		{
			m_Values = rox::core::Move(other.m_Values);
			m_Compare = other.m_Compare;
		}

		return *this;
	}

	template <typename T, typename TCompare>
	rox::core::usize PriorityQueue<T, TCompare>::Size() const noexcept
	{
		return m_Values.Size();
	}

	template <typename T, typename TCompare>
	rox::core::usize PriorityQueue<T, TCompare>::Capacity() const noexcept
	{
		return m_Values.Capacity();
	}

	template <typename T, typename TCompare>
	bool PriorityQueue<T, TCompare>::IsEmpty() const noexcept
	{
		return m_Values.IsEmpty();
	}

	template <typename T, typename TCompare>
	MemoryResource& PriorityQueue<T, TCompare>::GetResource() const noexcept
	{
		return m_Values.GetResource();
	}

	template <typename T, typename TCompare>
	const T& PriorityQueue<T, TCompare>::Top() const noexcept
	{
		ASSERT(!IsEmpty(), "PriorityQueue top requires a non-empty queue.");
		return m_Values[0];
	}

	template <typename T, typename TCompare>
	void PriorityQueue<T, TCompare>::ReserveRequired(rox::core::usize capacity) noexcept
	{
		m_Values.ReserveRequired(capacity);
	}

	template <typename T, typename TCompare>
	bool PriorityQueue<T, TCompare>::Reserve(const rox::core::usize capacity) noexcept
	{
		return m_Values.Reserve(capacity);
	}

	template <typename T, typename TCompare>
	void PriorityQueue<T, TCompare>::Clear() noexcept
	{
		m_Values.Clear();
	}

	template <typename T, typename TCompare>
	void PriorityQueue<T, TCompare>::Reset() noexcept
	{
		m_Values.Reset();
	}

	template <typename T, typename TCompare>
	void PriorityQueue<T, TCompare>::PushRequired(const T& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	{
		EmplaceRequired(value);
	}

	template <typename T, typename TCompare>
	void PriorityQueue<T, TCompare>::PushRequired(T&& value) noexcept
	{
		EmplaceRequired(rox::core::Move(value));
	}

	template <typename T, typename TCompare>
	bool PriorityQueue<T, TCompare>::Push(const T& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	{
		return Emplace(value);
	}

	template <typename T, typename TCompare>
	bool PriorityQueue<T, TCompare>::Push(T&& value) noexcept
	{
		return Emplace(rox::core::Move(value));
	}

	template <typename T, typename TCompare>
	template <typename... TArguments>
	bool PriorityQueue<T, TCompare>::Emplace(TArguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<T, TArguments&&...>
	{
		if (m_Values.Emplace(rox::core::Forward<TArguments>(arguments)...) == nullptr)
		{
			return false;
		}
		PushHeap(m_Values.View(), m_Compare);
		return true;
	}

	template <typename T, typename TCompare>
	template <typename... TArguments>
	void PriorityQueue<T, TCompare>::EmplaceRequired(TArguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<T, TArguments&&...>
	{
		m_Values.EmplaceRequired(rox::core::Forward<TArguments>(arguments)...);
		PushHeap(m_Values.View(), m_Compare);
	}

	template <typename T, typename TCompare>
	void PriorityQueue<T, TCompare>::Pop() noexcept
	{
		ASSERT(!IsEmpty(), "PriorityQueue pop requires a non-empty queue.");
		PopHeap(m_Values.View(), m_Compare);
		m_Values.PopBack();
	}

	template <typename T, typename TCompare>
	T PriorityQueue<T, TCompare>::ExtractTop() noexcept
	{
		ASSERT(!IsEmpty(), "PriorityQueue extract requires a non-empty queue.");
		PopHeap(m_Values.View(), m_Compare);
		T value(rox::core::Move(m_Values[m_Values.Size() - 1]));
		m_Values.PopBack();
		return value;
	}

	template <typename T, typename TCompare>
	void PriorityQueue<T, TCompare>::AssignRequired(const Span<const T> values) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	{
		Array<T> replacement(GetResource());
		replacement.AppendRequired(values);
		MakeHeap(replacement.View(), m_Compare);
		m_Values = rox::core::Move(replacement);
	}

	template <typename T, typename TCompare>
	bool PriorityQueue<T, TCompare>::Assign(const Span<const T> values) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	{
		Array<T> replacement(GetResource());
		if (!replacement.Append(values))
		{
			return false;
		}
		MakeHeap(replacement.View(), m_Compare);
		m_Values = rox::core::Move(replacement);
		return true;
	}
}
