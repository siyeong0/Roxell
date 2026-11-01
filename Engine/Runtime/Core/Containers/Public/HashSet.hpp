#pragma once

#include <HashMap.hpp>
#include <CompilerAttributes.h>
#include <IteratorTypes.h>
#include <Types.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>

namespace rox::core
{
	// Keys are immutable through the set. Resource, invalidation and threading rules match HashMap.
	template <typename TKey, typename THash = DefaultHash<TKey>, typename TEqual = DefaultEqual<TKey>> 
	class HashSet
	{
		struct Empty final
		{
		};

		using Table = HashMap<TKey, Empty, THash, TEqual>;

	public:
		struct InsertResult final
		{
			bool bSucceeded;
			bool bInserted;
		};

		class ConstIterator final
		{
		public:
			explicit ConstIterator(typename Table::ConstIterator iterator) noexcept;

			using iterator_category = core::ForwardIteratorTag;
			using value_type = TKey;
			using difference_type = isize;
			using reference = const TKey&;
			using pointer = const TKey*;
			ConstIterator() noexcept = default;

			[[nodiscard]] reference operator*() const noexcept;
			[[nodiscard]] pointer operator->() const noexcept;
			ROXELL_FORCE_INLINE ConstIterator& operator++() noexcept;
			ConstIterator operator++(int) noexcept;
			bool operator==(const ConstIterator&) const noexcept = default;

		private:
			typename Table::ConstIterator m_Iterator;
		};

		explicit HashSet(MemoryResource& resource, const THash& hash = {}, const TEqual& equal = {}) noexcept;

		[[nodiscard]] usize Size() const noexcept;
		[[nodiscard]] usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		void ReserveRequired(const usize minimumCapacity) noexcept;
		[[nodiscard]] bool Reserve(const usize minimumCapacity) noexcept;

		template <typename TKeyArgument>
		bool InsertRequired(TKeyArgument&& key) noexcept
		    requires requires(Table& table) { table.EmplaceRequired(core::Forward<TKeyArgument>(key)); };

		template <typename... TArguments>
		bool EmplaceRequired(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TKey, TArguments&&...>;

		template <typename TKeyArgument>
		[[nodiscard]] InsertResult Insert(TKeyArgument&& key) noexcept
		    requires requires(Table& table) { table.Emplace(core::Forward<TKeyArgument>(key)); };

		// Constructs the candidate before admission; constructor inputs may be consumed even on failure.
		template <typename... TArguments>
		[[nodiscard]] InsertResult Emplace(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TKey, TArguments&&...>;

		template <typename TLookup = TKey>
		[[nodiscard]] const TKey* Find(const TLookup& key) const noexcept
		    requires requires(const Table& table) { table.FindEntry(key); };

		template <typename TLookup = TKey>
		[[nodiscard]] bool Contains(const TLookup& key) const noexcept
		    requires requires(const Table& table) { table.Contains(key); };

		template <typename TLookup = TKey>
		bool Erase(const TLookup& key) noexcept
		    requires requires(Table& table) { table.Erase(key); };

		void Clear() noexcept;
		void Reset() noexcept;

		[[nodiscard]] ConstIterator begin() const noexcept;

		[[nodiscard]] ConstIterator end() const noexcept;

	private:
		Table m_Table;
	};
}

namespace rox::core
{
	template <typename TKey, typename THash, typename TEqual>
	HashSet<TKey, THash, TEqual>::ConstIterator::ConstIterator(typename Table::ConstIterator iterator) noexcept
	    : m_Iterator(iterator)
	{
	}

	template <typename TKey, typename THash, typename TEqual>
	typename HashSet<TKey, THash, TEqual>::ConstIterator::reference HashSet<TKey, THash, TEqual>::ConstIterator::operator*() const noexcept
	{
		return m_Iterator->GetKey();
	}

	template <typename TKey, typename THash, typename TEqual>
	typename HashSet<TKey, THash, TEqual>::ConstIterator::pointer HashSet<TKey, THash, TEqual>::ConstIterator::operator->() const noexcept
	{
		return core::AddressOf(operator*());
	}

	template <typename TKey, typename THash, typename TEqual>
	ROXELL_FORCE_INLINE HashSet<TKey, THash, TEqual>::ConstIterator& HashSet<TKey, THash, TEqual>::ConstIterator::operator++() noexcept
	{
		++m_Iterator;
		return *this;
	}

	template <typename TKey, typename THash, typename TEqual>
	HashSet<TKey, THash, TEqual>::ConstIterator HashSet<TKey, THash, TEqual>::ConstIterator::operator++(int) noexcept
	{
		auto previous = *this;
		++*this;
		return previous;
	}

	template <typename TKey, typename THash, typename TEqual>
	HashSet<TKey, THash, TEqual>::HashSet(MemoryResource& resource, const THash& hash, const TEqual& equal) noexcept
	    : m_Table(resource, hash, equal)
	{
	}

	template <typename TKey, typename THash, typename TEqual>
	usize HashSet<TKey, THash, TEqual>::Size() const noexcept
	{
		return m_Table.Size();
	}

	template <typename TKey, typename THash, typename TEqual>
	usize HashSet<TKey, THash, TEqual>::Capacity() const noexcept
	{
		return m_Table.Capacity();
	}

	template <typename TKey, typename THash, typename TEqual>
	bool HashSet<TKey, THash, TEqual>::IsEmpty() const noexcept
	{
		return m_Table.IsEmpty();
	}

	template <typename TKey, typename THash, typename TEqual>
	MemoryResource& HashSet<TKey, THash, TEqual>::GetResource() const noexcept
	{
		return m_Table.GetResource();
	}

	template <typename TKey, typename THash, typename TEqual>
	void HashSet<TKey, THash, TEqual>::ReserveRequired(const usize minimumCapacity) noexcept
	{
		m_Table.ReserveRequired(minimumCapacity);
	}

	template <typename TKey, typename THash, typename TEqual>
	bool HashSet<TKey, THash, TEqual>::Reserve(const usize minimumCapacity) noexcept
	{
		return m_Table.Reserve(minimumCapacity);
	}

	template <typename TKey, typename THash, typename TEqual>
	template <typename TKeyArgument>
	bool HashSet<TKey, THash, TEqual>::InsertRequired(TKeyArgument&& key) noexcept
	    requires requires(Table& table) { table.EmplaceRequired(core::Forward<TKeyArgument>(key)); }
	{
		return m_Table.EmplaceRequired(core::Forward<TKeyArgument>(key)).bInserted;
	}

	template <typename TKey, typename THash, typename TEqual>
	template <typename... TArguments>
	bool HashSet<TKey, THash, TEqual>::EmplaceRequired(TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TKey, TArguments&&...>
	{
		TKey key(core::Forward<TArguments>(arguments)...);
		return InsertRequired(core::Move(key));
	}

	template <typename TKey, typename THash, typename TEqual>
	template <typename TKeyArgument>
	typename HashSet<TKey, THash, TEqual>::InsertResult HashSet<TKey, THash, TEqual>::Insert(TKeyArgument&& key) noexcept
	    requires requires(Table& table) { table.Emplace(core::Forward<TKeyArgument>(key)); }
	{
		const auto result = m_Table.Emplace(core::Forward<TKeyArgument>(key));
		return {result.Value != nullptr, result.bInserted};
	}

	template <typename TKey, typename THash, typename TEqual>
	template <typename... TArguments>
	typename HashSet<TKey, THash, TEqual>::InsertResult HashSet<TKey, THash, TEqual>::Emplace(TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TKey, TArguments&&...>
	{
		TKey key(core::Forward<TArguments>(arguments)...);
		return Insert(core::Move(key));
	}

	template <typename TKey, typename THash, typename TEqual>
	template <typename TLookup>
	const TKey* HashSet<TKey, THash, TEqual>::Find(const TLookup& key) const noexcept
	    requires requires(const Table& table) { table.FindEntry(key); }
	{
		auto* const entry = m_Table.FindEntry(key);
		return entry == nullptr ? nullptr : core::AddressOf(entry->GetKey());
	}

	template <typename TKey, typename THash, typename TEqual>
	template <typename TLookup>
	bool HashSet<TKey, THash, TEqual>::Contains(const TLookup& key) const noexcept
	    requires requires(const Table& table) { table.Contains(key); }
	{
		return m_Table.Contains(key);
	}

	template <typename TKey, typename THash, typename TEqual>
	template <typename TLookup>
	bool HashSet<TKey, THash, TEqual>::Erase(const TLookup& key) noexcept
	    requires requires(Table& table) { table.Erase(key); }
	{
		return m_Table.Erase(key);
	}

	template <typename TKey, typename THash, typename TEqual>
	void HashSet<TKey, THash, TEqual>::Clear() noexcept
	{
		m_Table.Clear();
	}

	template <typename TKey, typename THash, typename TEqual>
	void HashSet<TKey, THash, TEqual>::Reset() noexcept
	{
		m_Table.Reset();
	}

	template <typename TKey, typename THash, typename TEqual>
	typename HashSet<TKey, THash, TEqual>::ConstIterator HashSet<TKey, THash, TEqual>::begin() const noexcept
	{
		return ConstIterator(m_Table.begin());
	}

	template <typename TKey, typename THash, typename TEqual>
	typename HashSet<TKey, THash, TEqual>::ConstIterator HashSet<TKey, THash, TEqual>::end() const noexcept
	{
		return ConstIterator(m_Table.end());
	}
}
