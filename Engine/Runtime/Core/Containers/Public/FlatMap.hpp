#pragma once

#include <Array.hpp>
#include <TypeVocabulary.hpp>
#include <Vocabulary.hpp>
#include <IteratorTypes.h>
#include <Types.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <Ordering.hpp>

#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// The resource and borrowed keys/comparator state outlive the map. Mutation requires exclusive access.
	template <typename TKey, typename TValue, typename TCompare = DefaultLess<TKey>> class FlatMap
	{
		static_assert(rox::core::IsNothrowMoveConstructible<TKey> && rox::core::IsNothrowDestructible<TKey>);
		static_assert(rox::core::IsNothrowMoveConstructible<TValue> && rox::core::IsNothrowDestructible<TValue>);
		static_assert(rox::core::IsNothrowCopyConstructible<TCompare> && rox::core::IsNothrowCopyAssignable<TCompare>);
		static_assert(rox::core::IsNothrowDestructible<TCompare>);
		static_assert(rox::core::IsNothrowInvocableR<bool, const TCompare&, const TKey&, const TKey&>);
		template <typename TLookup>
		static constexpr bool SUPPORTS_LOOKUP = rox::core::IsNothrowInvocableR<bool, const TCompare&, const TKey&, const TLookup&> && rox::core::IsNothrowInvocableR<bool, const TCompare&, const TLookup&, const TKey&>;

	public:
#if defined(_MSC_VER)
#pragma warning(push)
		// Over-aligned keys or values require padding inside their owning entry.
#pragma warning(disable : 4324)
#endif
		class Entry final
		{
			friend class FlatMap;
			
		public:
			template <typename TKeyArgument, typename... TArguments>
			Entry(TKeyArgument&& key, TArguments&&... arguments) noexcept
			    requires(rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> &&
			             rox::core::IsNothrowConstructible<TValue, TArguments && ...>);

			Entry& operator=(const Entry&) = delete;
			Entry& operator=(Entry&&) = delete;

			[[nodiscard]] const TKey& GetKey() const noexcept;

			TValue Value;

		private:
			// Keep entries immovable through the public view while retaining trivial
			// representation copying for the owning slot when its fields permit it.
			Entry(const Entry&) = default;
			Entry(Entry&&) = default;

		private:
			TKey m_Key;
		};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
		struct InsertResult final
		{
			TValue* Value;
			bool bInserted;
		};

	private:
		struct Slot final
		{
			Entry Item;

			template <typename TKeyArgument, typename... TArguments>
			Slot(TKeyArgument&& key, TArguments&&... arguments) noexcept
			    requires(rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> &&
			             rox::core::IsNothrowConstructible<TValue, TArguments && ...>);

			Slot(Slot&&) noexcept
			    requires rox::core::IsTriviallyCopyable<Entry>
			= default;

			Slot(Slot&& other) noexcept
			    requires(!rox::core::IsTriviallyCopyable<Entry>);

			Slot(const Slot&) noexcept
			    requires(rox::core::IsTriviallyCopyable<Entry> && rox::core::IsNothrowCopyConstructible<TKey> && rox::core::IsNothrowCopyConstructible<TValue>)
			= default;

			Slot(const Slot& other) noexcept
			    requires(!rox::core::IsTriviallyCopyable<Entry> && rox::core::IsNothrowCopyConstructible<TKey> && rox::core::IsNothrowCopyConstructible<TValue>);

			Slot& operator=(const Slot&) = delete;
			Slot& operator=(Slot&&) = delete;
		};

	public:
		template <bool TConst> class BasicIterator final
		{
			template <bool> friend class BasicIterator;
			using MapType = rox::core::Conditional<TConst, const FlatMap, FlatMap>;

		public:
			BasicIterator(MapType& map, rox::core::usize index) noexcept;

			using iterator_category = rox::core::ForwardIteratorTag;
			using value_type = Entry;
			using difference_type = rox::core::isize;
			using reference = rox::core::Conditional<TConst, const Entry&, Entry&>;
			using pointer = rox::core::Conditional<TConst, const Entry*, Entry*>;
			BasicIterator() noexcept = default;
			BasicIterator(const BasicIterator&) noexcept = default;
			BasicIterator& operator=(const BasicIterator&) noexcept = default;

			BasicIterator(const BasicIterator<false>& other) noexcept
			    requires TConst;

			[[nodiscard]] reference operator*() const noexcept;
			[[nodiscard]] pointer operator->() const noexcept;

			BasicIterator& operator++() noexcept;
			BasicIterator operator++(int) noexcept;

			bool operator==(const BasicIterator&) const noexcept = default;

		private:
			MapType* m_Map = nullptr;
			rox::core::usize m_Index = 0;
		};

		using Iterator = BasicIterator<false>;
		using ConstIterator = BasicIterator<true>;

		explicit FlatMap(MemoryResource& resource, const TCompare& compare = {}) noexcept;

		FlatMap(const FlatMap& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<Slot>;

		FlatMap(FlatMap&& other) noexcept;

		FlatMap& operator=(const FlatMap& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<Slot>;

		FlatMap& operator=(FlatMap&& other) noexcept;

		~FlatMap() noexcept = default;

		[[nodiscard]] rox::core::usize Size() const noexcept;
		[[nodiscard]] rox::core::usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		[[nodiscard]] Iterator begin() noexcept;
		[[nodiscard]] Iterator end() noexcept;

		[[nodiscard]] ConstIterator begin() const noexcept;
		[[nodiscard]] ConstIterator end() const noexcept;

		template <typename TLookup = TKey>
		[[nodiscard]] Iterator LowerBound(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] ConstIterator LowerBound(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] Iterator UpperBound(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] ConstIterator UpperBound(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		// No-op reserve preserves views; growth invalidates every entry view.
		[[nodiscard]] bool Reserve(const rox::core::usize capacity) noexcept;
		[[nodiscard]] bool Grow(const rox::core::usize capacity) noexcept;
		void ReserveRequired(const rox::core::usize capacity) noexcept;

		// Both invalidate all views. Clear retains capacity; Reset releases the allocation.
		void Clear() noexcept;
		void Reset() noexcept;

		// Duplicate admission consumes nothing. A pending entry preserves aliased arguments before relocation.
		template <typename TKeyArgument, typename... TArguments>
		InsertResult EmplaceRequired(TKeyArgument&& key, TArguments&&... arguments) noexcept
		    requires(rox::core::IsSame<rox::core::RemoveCvref<TKeyArgument>, TKey> &&
		             rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> &&
		             rox::core::IsNothrowConstructible<TValue, TArguments && ...>);

		template <typename TKeyArgument, typename TValueArgument>
		InsertResult InsertRequired(TKeyArgument&& key, TValueArgument&& value) noexcept
		    requires requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); };

		// Null reports storage allocation failure; arguments and existing entries remain unchanged.
		// Element constructors retain their own allocation/failure contracts.
		template <typename TKeyArgument, typename... TArguments>
		[[nodiscard]] InsertResult Emplace(TKeyArgument&& key, TArguments&&... arguments) noexcept
		    requires requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TArguments>(arguments)...); };

		template <typename TKeyArgument, typename TValueArgument>
		[[nodiscard]] InsertResult Insert(TKeyArgument&& key, TValueArgument&& value) noexcept
		    requires requires { Emplace(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); };

		template <typename TLookup = TKey>
		[[nodiscard]] TValue* Find(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] const TValue* Find(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] bool Contains(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] Entry* FindEntry(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] const Entry* FindEntry(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] TValue& Get(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] const TValue& Get(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		// Indexing requires an existing key and never inserts a default value.
		template <typename TLookup = TKey>
		[[nodiscard]] TValue& operator[](const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] const TValue& operator[](const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		[[nodiscard]] Entry& At(const rox::core::usize index) noexcept;

		[[nodiscard]] const Entry& At(const rox::core::usize index) const noexcept;

		// Searches before relocation so the probe may borrow an erased key.
		template <typename TLookup = TKey>
		bool Erase(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TKeyArgument, typename TValueArgument>
		InsertResult InsertOrAssignRequired(TKeyArgument&& key, TValueArgument&& value) noexcept
		    requires(rox::core::IsNothrowMoveAssignable<TValue> &&
		             requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); });

		// A failed insertion leaves the arguments and existing entries unchanged.
		template <typename TKeyArgument, typename TValueArgument>
		[[nodiscard]] InsertResult InsertOrAssign(TKeyArgument&& key, TValueArgument&& value) noexcept
		    requires requires { InsertOrAssignRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); };

	private:
		template <typename TLookup> [[nodiscard]] rox::core::usize lowerBoundIndex(const TLookup& key) const noexcept;
		template <typename TLookup> [[nodiscard]] rox::core::usize upperBoundIndex(const TLookup& key) const noexcept;
		template <typename TLookup> [[nodiscard]] rox::core::usize findIndex(const TLookup& key) const noexcept;

	private:
		Array<Slot> m_Entries;
		TCompare m_Compare;
	};
}

namespace rox::core
{
	template <typename TKey, typename TValue, typename TCompare>
	template <typename TKeyArgument, typename... TArguments>
	FlatMap<TKey, TValue, TCompare>::Entry::Entry(TKeyArgument&& key, TArguments&&... arguments) noexcept
	    requires(rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> && rox::core::IsNothrowConstructible<TValue, TArguments && ...>)
	    : m_Key(rox::core::Forward<TKeyArgument>(key))
	    , Value(rox::core::Forward<TArguments>(arguments)...)
	{
	}

	template <typename TKey, typename TValue, typename TCompare>
	const TKey& FlatMap<TKey, TValue, TCompare>::Entry::GetKey() const noexcept
	{
		return m_Key;
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TKeyArgument, typename... TArguments>
	FlatMap<TKey, TValue, TCompare>::Slot::Slot(TKeyArgument&& key, TArguments&&... arguments) noexcept
	    requires(rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> && rox::core::IsNothrowConstructible<TValue, TArguments && ...>)
	    : Item(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TArguments>(arguments)...)
	{
	}

	template <typename TKey, typename TValue, typename TCompare>
	FlatMap<TKey, TValue, TCompare>::Slot::Slot(Slot&& other) noexcept
	    requires(!rox::core::IsTriviallyCopyable<Entry>)
	    : Item(rox::core::Move(other.Item.m_Key), rox::core::Move(other.Item.Value))
	{
	}

	template <typename TKey, typename TValue, typename TCompare>
	FlatMap<TKey, TValue, TCompare>::Slot::Slot(const Slot& other) noexcept
	    requires(!rox::core::IsTriviallyCopyable<Entry> && rox::core::IsNothrowCopyConstructible<TKey> && rox::core::IsNothrowCopyConstructible<TValue>)
	    : Item(other.Item.m_Key, other.Item.Value)
	{
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <bool TConst>
	FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst>::BasicIterator(MapType& map, rox::core::usize index) noexcept
	    : m_Map(&map)
	    , m_Index(index)
	{
		ASSERT(index <= map.Size(), "FlatMap iterator position must lie within its map.");
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <bool TConst>
	FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst>::BasicIterator(const BasicIterator<false>& other) noexcept
	    requires TConst
	    : m_Map(other.m_Map)
	    , m_Index(other.m_Index)
	{
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <bool TConst>
	typename FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst>::reference FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst>::operator*() const noexcept
	{
		ASSERT(m_Map != nullptr && m_Index < m_Map->Size(), "FlatMap iterator must identify a live entry.");
		return m_Map->m_Entries[m_Index].Item;
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <bool TConst>
	typename FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst>::pointer FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst>::operator->() const noexcept
	{
		return rox::core::AddressOf(operator*());
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <bool TConst>
	FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst>& FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst>::operator++() noexcept
	{
		ASSERT(m_Map != nullptr && m_Index < m_Map->Size(), "FlatMap cannot increment an end iterator.");
		++m_Index;
		return *this;
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <bool TConst>
	FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst> FlatMap<TKey, TValue, TCompare>::BasicIterator<TConst>::operator++(int) noexcept
	{
		auto previous = *this;
		++*this;
		return previous;
	}

	template <typename TKey, typename TValue, typename TCompare>
	FlatMap<TKey, TValue, TCompare>::FlatMap(MemoryResource& resource, const TCompare& compare) noexcept
	    : m_Entries(resource)
	    , m_Compare(compare)
	{
	}

	template <typename TKey, typename TValue, typename TCompare>
	FlatMap<TKey, TValue, TCompare>::FlatMap(const FlatMap& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<Slot>
	    : m_Entries(other.m_Entries)
	    , m_Compare(other.m_Compare)
	{
	}

	template <typename TKey, typename TValue, typename TCompare>
	FlatMap<TKey, TValue, TCompare>::FlatMap(FlatMap&& other) noexcept
	    : m_Entries(rox::core::Move(other.m_Entries))
	    , m_Compare(other.m_Compare)
	{
	}

	template <typename TKey, typename TValue, typename TCompare>
	FlatMap<TKey, TValue, TCompare>& FlatMap<TKey, TValue, TCompare>::operator=(const FlatMap& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<Slot>
	{
		if (this != &other)
		{
			m_Entries = other.m_Entries;
			m_Compare = other.m_Compare;
		}

		return *this;
	}

	template <typename TKey, typename TValue, typename TCompare>
	FlatMap<TKey, TValue, TCompare>& FlatMap<TKey, TValue, TCompare>::operator=(FlatMap&& other) noexcept
	{
		if (this != &other)
		{
			m_Entries = rox::core::Move(other.m_Entries);
			m_Compare = other.m_Compare;
		}

		return *this;
	}

	template <typename TKey, typename TValue, typename TCompare>
	rox::core::usize FlatMap<TKey, TValue, TCompare>::Size() const noexcept
	{
		return m_Entries.Size();
	}

	template <typename TKey, typename TValue, typename TCompare>
	rox::core::usize FlatMap<TKey, TValue, TCompare>::Capacity() const noexcept
	{
		return m_Entries.Capacity();
	}

	template <typename TKey, typename TValue, typename TCompare>
	bool FlatMap<TKey, TValue, TCompare>::IsEmpty() const noexcept
	{
		return m_Entries.IsEmpty();
	}

	template <typename TKey, typename TValue, typename TCompare>
	MemoryResource& FlatMap<TKey, TValue, TCompare>::GetResource() const noexcept
	{
		return m_Entries.GetResource();
	}

	template <typename TKey, typename TValue, typename TCompare>
	typename FlatMap<TKey, TValue, TCompare>::Iterator FlatMap<TKey, TValue, TCompare>::begin() noexcept
	{
		return {*this, 0};
	}

	template <typename TKey, typename TValue, typename TCompare>
	typename FlatMap<TKey, TValue, TCompare>::Iterator FlatMap<TKey, TValue, TCompare>::end() noexcept
	{
		return {*this, Size()};
	}

	template <typename TKey, typename TValue, typename TCompare>
	typename FlatMap<TKey, TValue, TCompare>::ConstIterator FlatMap<TKey, TValue, TCompare>::begin() const noexcept
	{
		return {*this, 0};
	}

	template <typename TKey, typename TValue, typename TCompare>
	typename FlatMap<TKey, TValue, TCompare>::ConstIterator FlatMap<TKey, TValue, TCompare>::end() const noexcept
	{
		return {*this, Size()};
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	typename FlatMap<TKey, TValue, TCompare>::Iterator FlatMap<TKey, TValue, TCompare>::LowerBound(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		return {*this, lowerBoundIndex(key)};
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	typename FlatMap<TKey, TValue, TCompare>::ConstIterator FlatMap<TKey, TValue, TCompare>::LowerBound(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		return {*this, lowerBoundIndex(key)};
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	typename FlatMap<TKey, TValue, TCompare>::Iterator FlatMap<TKey, TValue, TCompare>::UpperBound(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		return {*this, upperBoundIndex(key)};
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	typename FlatMap<TKey, TValue, TCompare>::ConstIterator FlatMap<TKey, TValue, TCompare>::UpperBound(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		return {*this, upperBoundIndex(key)};
	}

	template <typename TKey, typename TValue, typename TCompare>
	bool FlatMap<TKey, TValue, TCompare>::Reserve(const rox::core::usize capacity) noexcept
	{
		return m_Entries.Reserve(capacity);
	}

	template <typename TKey, typename TValue, typename TCompare>
	bool FlatMap<TKey, TValue, TCompare>::Grow(const rox::core::usize capacity) noexcept
	{
		return m_Entries.Grow(capacity);
	}

	template <typename TKey, typename TValue, typename TCompare>
	void FlatMap<TKey, TValue, TCompare>::ReserveRequired(const rox::core::usize capacity) noexcept
	{
		m_Entries.ReserveRequired(capacity);
	}

	template <typename TKey, typename TValue, typename TCompare>
	void FlatMap<TKey, TValue, TCompare>::Clear() noexcept
	{
		m_Entries.Clear();
	}

	template <typename TKey, typename TValue, typename TCompare>
	void FlatMap<TKey, TValue, TCompare>::Reset() noexcept
	{
		m_Entries.Reset();
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TKeyArgument, typename... TArguments>
	typename FlatMap<TKey, TValue, TCompare>::InsertResult FlatMap<TKey, TValue, TCompare>::EmplaceRequired(TKeyArgument&& key, TArguments&&... arguments) noexcept
	    requires(rox::core::IsSame<rox::core::RemoveCvref<TKeyArgument>, TKey> &&
	             rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> &&
	             rox::core::IsNothrowConstructible<TValue, TArguments && ...>)
	{
		const rox::core::usize index = lowerBoundIndex(key);

		if (index != Size() && !rox::core::AsConst(m_Compare)(rox::core::AsConst(key), m_Entries[index].Item.GetKey()))
		{
			return {rox::core::AddressOf(m_Entries[index].Item.Value), false};
		}

		return {
		    rox::core::AddressOf(m_Entries.EmplaceAtRequired(index, rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TArguments>(arguments)...).Item.Value),
		    true};
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TKeyArgument, typename TValueArgument>
	typename FlatMap<TKey, TValue, TCompare>::InsertResult FlatMap<TKey, TValue, TCompare>::InsertRequired(TKeyArgument&& key, TValueArgument&& value) noexcept
	    requires requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); }
	{
		return EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value));
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TKeyArgument, typename... TArguments>
	typename FlatMap<TKey, TValue, TCompare>::InsertResult FlatMap<TKey, TValue, TCompare>::Emplace(TKeyArgument&& key, TArguments&&... arguments) noexcept
	    requires requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TArguments>(arguments)...); }
	{
		const rox::core::usize index = lowerBoundIndex(key);

		if (index != Size() && !rox::core::AsConst(m_Compare)(rox::core::AsConst(key), m_Entries[index].Item.GetKey()))
		{
			return {rox::core::AddressOf(m_Entries[index].Item.Value), false};
		}

		Slot* const slot = m_Entries.EmplaceAt(index, rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TArguments>(arguments)...);
		return slot == nullptr ? InsertResult{nullptr, false} : InsertResult{rox::core::AddressOf(slot->Item.Value), true};
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TKeyArgument, typename TValueArgument>
	typename FlatMap<TKey, TValue, TCompare>::InsertResult FlatMap<TKey, TValue, TCompare>::Insert(TKeyArgument&& key, TValueArgument&& value) noexcept
	    requires requires { Emplace(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); }
	{
		return Emplace(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value));
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	TValue* FlatMap<TKey, TValue, TCompare>::Find(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		auto* entry = FindEntry(key);
		return entry == nullptr ? nullptr : rox::core::AddressOf(entry->Value);
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	const TValue* FlatMap<TKey, TValue, TCompare>::Find(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto* entry = FindEntry(key);
		return entry == nullptr ? nullptr : rox::core::AddressOf(entry->Value);
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	bool FlatMap<TKey, TValue, TCompare>::Contains(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		return FindEntry(key) != nullptr;
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	typename FlatMap<TKey, TValue, TCompare>::Entry* FlatMap<TKey, TValue, TCompare>::FindEntry(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key);
		return index == Size() ? nullptr : rox::core::AddressOf(m_Entries[index].Item);
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	const typename FlatMap<TKey, TValue, TCompare>::Entry* FlatMap<TKey, TValue, TCompare>::FindEntry(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key);
		return index == Size() ? nullptr : rox::core::AddressOf(m_Entries[index].Item);
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	TValue& FlatMap<TKey, TValue, TCompare>::Get(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		auto* value = Find(key);
		ASSERT(value != nullptr, "FlatMap::Get requires an existing key.");
		return *value;
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	const TValue& FlatMap<TKey, TValue, TCompare>::Get(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto* value = Find(key);
		ASSERT(value != nullptr, "FlatMap::Get requires an existing key.");
		return *value;
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	TValue& FlatMap<TKey, TValue, TCompare>::operator[](const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		return Get(key);
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	const TValue& FlatMap<TKey, TValue, TCompare>::operator[](const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		return Get(key);
	}

	template <typename TKey, typename TValue, typename TCompare>
	typename FlatMap<TKey, TValue, TCompare>::Entry& FlatMap<TKey, TValue, TCompare>::At(const rox::core::usize index) noexcept
	{
		ASSERT(index < Size(), "FlatMap index must identify a live entry.");
		return m_Entries[index].Item;
	}

	template <typename TKey, typename TValue, typename TCompare>
	const typename FlatMap<TKey, TValue, TCompare>::Entry& FlatMap<TKey, TValue, TCompare>::At(const rox::core::usize index) const noexcept
	{
		ASSERT(index < Size(), "FlatMap index must identify a live entry.");
		return m_Entries[index].Item;
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	bool FlatMap<TKey, TValue, TCompare>::Erase(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key);

		if (index == Size())
		{
			return false;
		}

		m_Entries.RemoveAt(index);
		return true;
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TKeyArgument, typename TValueArgument>
	typename FlatMap<TKey, TValue, TCompare>::InsertResult FlatMap<TKey, TValue, TCompare>::InsertOrAssignRequired(TKeyArgument&& key, TValueArgument&& value) noexcept
	    requires(rox::core::IsNothrowMoveAssignable<TValue> &&
	             requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); })
	{
		const rox::core::usize index = lowerBoundIndex(key);
		if (index == Size() || rox::core::AsConst(m_Compare)(rox::core::AsConst(key), m_Entries[index].Item.GetKey()))
		{
			return {
			    rox::core::AddressOf(m_Entries.EmplaceAtRequired(index, rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)).Item.Value),
			    true
			};
		}
		TValue* const existing = rox::core::AddressOf(m_Entries[index].Item.Value);
		TValue pending(rox::core::Forward<TValueArgument>(value));
		*existing = rox::core::Move(pending);
		return {existing, false};
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TKeyArgument, typename TValueArgument>
	typename FlatMap<TKey, TValue, TCompare>::InsertResult FlatMap<TKey, TValue, TCompare>::InsertOrAssign(TKeyArgument&& key, TValueArgument&& value) noexcept
	    requires requires { InsertOrAssignRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); }
	{
		const rox::core::usize index = lowerBoundIndex(key);
		if (index == Size() || rox::core::AsConst(m_Compare)(rox::core::AsConst(key), m_Entries[index].Item.GetKey()))
		{
			auto* const entry = m_Entries.EmplaceAt(index, rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value));
			return {entry == nullptr ? nullptr : rox::core::AddressOf(entry->Item.Value), entry != nullptr};
		}
		TValue* const existing = rox::core::AddressOf(m_Entries[index].Item.Value);
		TValue pending(rox::core::Forward<TValueArgument>(value));
		*existing = rox::core::Move(pending);
		return {existing, false};
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	rox::core::usize FlatMap<TKey, TValue, TCompare>::lowerBoundIndex(const TLookup& key) const noexcept
	{
		const Slot* const entries = m_Entries.Data();
		rox::core::usize first = 0;
		rox::core::usize count = Size();

		if (count == 0)
		{
			return 0;
		}

		while (count > 1)
		{
			const rox::core::usize half = count / 2;
			const bool bBefore = m_Compare(entries[first + half - 1].Item.GetKey(), key);
			first = bBefore ? first + half : first;
			count -= half;
		}

		return first + static_cast<rox::core::usize>(m_Compare(entries[first].Item.GetKey(), key));
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	rox::core::usize FlatMap<TKey, TValue, TCompare>::upperBoundIndex(const TLookup& key) const noexcept
	{
		const Slot* const entries = m_Entries.Data();
		rox::core::usize first = 0;
		rox::core::usize count = Size();

		if (count == 0)
		{
			return 0;
		}

		while (count > 1)
		{
			const rox::core::usize half = count / 2;
			const bool bAfter = m_Compare(key, entries[first + half - 1].Item.GetKey());
			first = bAfter ? first : first + half;
			count -= half;
		}

		return first + static_cast<rox::core::usize>(!m_Compare(key, entries[first].Item.GetKey()));
	}

	template <typename TKey, typename TValue, typename TCompare>
	template <typename TLookup>
	rox::core::usize FlatMap<TKey, TValue, TCompare>::findIndex(const TLookup& key) const noexcept
	{
		const auto index = lowerBoundIndex(key);
		return index != Size() && !m_Compare(key, m_Entries[index].Item.GetKey()) ? index : Size();
	}
}
