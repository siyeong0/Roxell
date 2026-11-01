#pragma once

#include <Assertion.h>
#include <TypeVocabulary.hpp>
#include <IteratorTypes.h>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>

namespace rox::core
{
	template <typename T> class IntrusiveListHook;
	template <typename T, IntrusiveListHook<T> T::* Member> class IntrusiveList;

	template <typename T> 
	class IntrusiveListHook final
	{
		template <typename U, IntrusiveListHook<U> U::* HookMember> friend class IntrusiveList;

	public:
		IntrusiveListHook() noexcept = default;
		~IntrusiveListHook() noexcept;

		IntrusiveListHook(const IntrusiveListHook&) = delete;
		IntrusiveListHook& operator=(const IntrusiveListHook&) = delete;
		IntrusiveListHook(IntrusiveListHook&&) = delete;
		IntrusiveListHook& operator=(IntrusiveListHook&&) = delete;

		[[nodiscard]] bool IsLinked() const noexcept;

	private:
		T* m_Previous = nullptr;
		T* m_Next = nullptr;
		const void* m_Owner = nullptr;
	};

	// Objects keep their address and outlive their membership; distinct hooks allow independent lists.
	template <typename T, IntrusiveListHook<T> T::* Member> class IntrusiveList final
	{
	public:
		template <bool Const> 
		class BasicIterator final
		{
			template <bool> friend class BasicIterator;
			using ListType = core::Conditional<Const, const IntrusiveList, IntrusiveList>;

		public:
			using iterator_category = core::ForwardIteratorTag;
			using value_type = T;
			using difference_type = isize;
			using reference = core::Conditional<Const, const T&, T&>;
			using pointer = core::Conditional<Const, const T*, T*>;
			BasicIterator() noexcept = default;
			BasicIterator(const BasicIterator&) noexcept = default;
			BasicIterator& operator=(const BasicIterator&) noexcept = default;

			BasicIterator(const BasicIterator<false>& other) noexcept
			    requires Const;

			[[nodiscard]] reference operator*() const noexcept;
			[[nodiscard]] pointer operator->() const noexcept;

			BasicIterator& operator++() noexcept;
			BasicIterator operator++(int) noexcept;

			template <bool OtherConst>
			[[nodiscard]] bool operator==(const BasicIterator<OtherConst>& other) const noexcept;

			BasicIterator(ListType& owner, pointer item) noexcept;

		private:
			ListType* m_Owner = nullptr;
			pointer m_Item = nullptr;
		};

		using Iterator = BasicIterator<false>;
		using ConstIterator = BasicIterator<true>;
		IntrusiveList() noexcept = default;
		~IntrusiveList() noexcept;

		IntrusiveList(const IntrusiveList&) = delete;
		IntrusiveList& operator=(const IntrusiveList&) = delete;
		IntrusiveList(IntrusiveList&&) = delete;
		IntrusiveList& operator=(IntrusiveList&&) = delete;

		[[nodiscard]] usize Size() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] bool Contains(const T& value) const noexcept;

		[[nodiscard]] T* Front() noexcept;
		[[nodiscard]] const T* Front() const noexcept;

		[[nodiscard]] T* Back() noexcept;
		[[nodiscard]] const T* Back() const noexcept;

		[[nodiscard]] Iterator begin() noexcept;
		[[nodiscard]] Iterator end() noexcept;

		[[nodiscard]] ConstIterator begin() const noexcept;
		[[nodiscard]] ConstIterator end() const noexcept;

		[[nodiscard]] T* Next(const T& value) noexcept;
		[[nodiscard]] const T* Next(const T& value) const noexcept;

		[[nodiscard]] T* Previous(const T& value) noexcept;
		[[nodiscard]] const T* Previous(const T& value) const noexcept;

		void PushFront(T& value) noexcept;
		void PushBack(T& value) noexcept;

		void InsertBefore(T& anchor, T& value) noexcept;

		[[nodiscard]] T* PopFront() noexcept;
		[[nodiscard]] T* PopBack() noexcept;

		void Remove(T& value) noexcept;
		void Clear() noexcept;

	private:
		void insert(T& value, T* previous, T* next) noexcept;

	private:
		T* m_First = nullptr;
		T* m_Last = nullptr;
		usize m_Size = 0;
	};
}

namespace rox::core
{
	template <typename T>
	IntrusiveListHook<T>::~IntrusiveListHook() noexcept
	{
		ASSERT(m_Owner == nullptr, "Intrusive hook must be unlinked before destruction.");
	}

	template <typename T>
	bool IntrusiveListHook<T>::IsLinked() const noexcept
	{
		return m_Owner != nullptr;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	template <bool Const>
	IntrusiveList<T, Member>::BasicIterator<Const>::BasicIterator(const BasicIterator<false>& other) noexcept
	    requires Const
	    : m_Owner(other.m_Owner)
	    , m_Item(other.m_Item)
	{
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	template <bool Const>
	typename IntrusiveList<T, Member>::BasicIterator<Const>::reference IntrusiveList<T, Member>::BasicIterator<Const>::operator*() const noexcept
	{
		ASSERT(m_Owner != nullptr && m_Item != nullptr && m_Owner->Contains(*m_Item), "Intrusive iterator must identify a live item.");
		return *m_Item;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	template <bool Const>
	typename IntrusiveList<T, Member>::BasicIterator<Const>::pointer IntrusiveList<T, Member>::BasicIterator<Const>::operator->() const noexcept
	{
		return core::AddressOf(operator*());
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	template <bool Const>
	IntrusiveList<T, Member>::BasicIterator<Const>& IntrusiveList<T, Member>::BasicIterator<Const>::operator++() noexcept
	{
		m_Item = (operator*().*Member).m_Next;
		return *this;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	template <bool Const>
	IntrusiveList<T, Member>::BasicIterator<Const> IntrusiveList<T, Member>::BasicIterator<Const>::operator++(int) noexcept
	{
		auto previous = *this;
		++*this;
		return previous;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	template <bool Const>
	template <bool OtherConst>
	bool IntrusiveList<T, Member>::BasicIterator<Const>::operator==(const BasicIterator<OtherConst>& other) const noexcept
	{
		return m_Owner == other.m_Owner && m_Item == other.m_Item;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	template <bool Const>
	IntrusiveList<T, Member>::BasicIterator<Const>::BasicIterator(ListType& owner, pointer item) noexcept
	    : m_Owner(&owner)
	    , m_Item(item)
	{
		ASSERT(item == nullptr || owner.Contains(*item), "Intrusive iterator requires an item from its list.");
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	IntrusiveList<T, Member>::~IntrusiveList() noexcept
	{
		Clear();
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	usize IntrusiveList<T, Member>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	bool IntrusiveList<T, Member>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	bool IntrusiveList<T, Member>::Contains(const T& value) const noexcept
	{
		return (value.*Member).m_Owner == this;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	T* IntrusiveList<T, Member>::Front() noexcept
	{
		return m_First;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	const T* IntrusiveList<T, Member>::Front() const noexcept
	{
		return m_First;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	T* IntrusiveList<T, Member>::Back() noexcept
	{
		return m_Last;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	const T* IntrusiveList<T, Member>::Back() const noexcept
	{
		return m_Last;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	typename IntrusiveList<T, Member>::Iterator IntrusiveList<T, Member>::begin() noexcept
	{
		return {*this, m_First};
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	typename IntrusiveList<T, Member>::Iterator IntrusiveList<T, Member>::end() noexcept
	{
		return {*this, nullptr};
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	typename IntrusiveList<T, Member>::ConstIterator IntrusiveList<T, Member>::begin() const noexcept
	{
		return {*this, m_First};
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	typename IntrusiveList<T, Member>::ConstIterator IntrusiveList<T, Member>::end() const noexcept
	{
		return {*this, nullptr};
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	T* IntrusiveList<T, Member>::Next(const T& value) noexcept
	{
		ASSERT(Contains(value), "Intrusive operation requires membership in this list.");
		return (value.*Member).m_Next;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	const T* IntrusiveList<T, Member>::Next(const T& value) const noexcept
	{
		ASSERT(Contains(value), "Intrusive operation requires membership in this list.");
		return (value.*Member).m_Next;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	T* IntrusiveList<T, Member>::Previous(const T& value) noexcept
	{
		ASSERT(Contains(value), "Intrusive operation requires membership in this list.");
		return (value.*Member).m_Previous;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	const T* IntrusiveList<T, Member>::Previous(const T& value) const noexcept
	{
		ASSERT(Contains(value), "Intrusive operation requires membership in this list.");
		return (value.*Member).m_Previous;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	void IntrusiveList<T, Member>::PushFront(T& value) noexcept
	{
		insert(value, nullptr, m_First);
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	void IntrusiveList<T, Member>::PushBack(T& value) noexcept
	{
		insert(value, m_Last, nullptr);
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	void IntrusiveList<T, Member>::InsertBefore(T& anchor, T& value) noexcept
	{
		ASSERT(Contains(anchor), "Intrusive operation requires membership in this list.");
		insert(value, (anchor.*Member).m_Previous, core::AddressOf(anchor));
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	T* IntrusiveList<T, Member>::PopFront() noexcept
	{
		T* value = m_First;

		if (value != nullptr)
		{
			Remove(*value);
		}

		return value;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	T* IntrusiveList<T, Member>::PopBack() noexcept
	{
		T* value = m_Last;

		if (value != nullptr)
		{
			Remove(*value);
		}

		return value;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	void IntrusiveList<T, Member>::Remove(T& value) noexcept
	{
		ASSERT(Contains(value), "Intrusive operation requires membership in this list.");
		auto& hook = value.*Member;

		if (hook.m_Previous != nullptr)
		{
			(hook.m_Previous->*Member).m_Next = hook.m_Next;
		}
		else
		{
			m_First = hook.m_Next;
		}

		if (hook.m_Next != nullptr)
		{
			(hook.m_Next->*Member).m_Previous = hook.m_Previous;
		}
		else
		{
			m_Last = hook.m_Previous;
		}
		hook.m_Previous = nullptr;
		hook.m_Next = nullptr;
		hook.m_Owner = nullptr;
		--m_Size;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	void IntrusiveList<T, Member>::Clear() noexcept
	{
		while (m_First != nullptr)
		{
			auto& hook = m_First->*Member;
			ASSERT(hook.m_Owner == this, "Intrusive operation requires membership in this list.");
			m_First = hook.m_Next;
			hook.m_Previous = nullptr;
			hook.m_Next = nullptr;
			hook.m_Owner = nullptr;
		}
		m_Last = nullptr;
		m_Size = 0;
	}

	template <typename T, IntrusiveListHook<T> T::* Member>
	void IntrusiveList<T, Member>::insert(T& value, T* previous, T* next) noexcept
	{
		ASSERT(!(value.*Member).IsLinked(), "Intrusive insertion requires an unlinked hook.");
		ASSERT(m_Size < NumericLimits<usize>::max(), "Intrusive list size must remain representable.");
		auto& hook = value.*Member;
		hook.m_Previous = previous;
		hook.m_Next = next;
		hook.m_Owner = this;

		if (previous != nullptr)
		{
			(previous->*Member).m_Next = core::AddressOf(value);
		}
		else
		{
			m_First = core::AddressOf(value);
		}

		if (next != nullptr)
		{
			(next->*Member).m_Previous = core::AddressOf(value);
		}
		else
		{
			m_Last = core::AddressOf(value);
		}
		++m_Size;
	}
}
