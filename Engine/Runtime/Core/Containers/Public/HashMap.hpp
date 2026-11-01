#pragma once

#include <Assertion.h>
#include <ByteOperations.hpp>
#include <ByteOrder.h>
#include <CompilerAttributes.h>
#include <TypeVocabulary.hpp>
#include <Vocabulary.hpp>
#include <IteratorTypes.h>
#include <Types.h>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <Hashing.hpp>
#include <MemoryResource.h>

#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// The resource and borrowed key storage outlive the table. Mutation requires exclusive access.
	// Mutations may invalidate all entries and iterators; iteration order is unspecified.
	template <typename TKey, typename TValue, typename THash = DefaultHash<TKey>, typename TEqual = DefaultEqual<TKey>>
	class HashMap
	{
		static_assert(IsNothrowMoveConstructible<TKey> && IsNothrowDestructible<TKey>);
		static_assert(IsNothrowMoveConstructible<TValue> && IsNothrowDestructible<TValue>);
		static_assert(IsNothrowCopyConstructible<THash> && IsNothrowCopyAssignable<THash>);
		static_assert(IsNothrowCopyConstructible<TEqual> && IsNothrowCopyAssignable<TEqual>);
		static_assert(IsNothrowDestructible<THash> && IsNothrowDestructible<TEqual>);
		static_assert(IsNothrowInvocableR<usize, const THash&, const TKey&>);
		static_assert(IsNothrowInvocableR<bool, const TEqual&, const TKey&, const TKey&>);
		template <typename TLookup>
		static constexpr bool SUPPORTS_LOOKUP = IsNothrowInvocableR<usize, const THash&, const TLookup&> && IsNothrowInvocableR<bool, const TEqual&, const TKey&, const TLookup&>;

	public:
#if defined(_MSC_VER)
#pragma warning(push)
		// Over-aligned keys or values require padding inside their owning entry.
#pragma warning(disable : 4324)
#endif
		class Entry final
		{
			friend class HashMap;
			TKey m_Key;

			// Keep public entries immovable while permitting representation copying
			// inside the owning table when both fields are trivial.
			Entry(const Entry&) = default;
			Entry(Entry&&) = default;

		public:
			template <typename TKeyArgument, typename... TArguments>
			Entry(TKeyArgument&& key, TArguments&&... arguments) noexcept
			    requires(IsNothrowConstructible<TKey, TKeyArgument &&> &&
			             IsNothrowConstructible<TValue, TArguments && ...>);

			Entry& operator=(const Entry&) = delete;
			Entry& operator=(Entry&&) = delete;

			[[nodiscard]] const TKey& GetKey() const noexcept;

			// Set-like tables need no payload storage for an empty mapped type.
#if defined(_MSC_VER) && !defined(__clang__)
			[[msvc::no_unique_address]] TValue Value;
#else
			[[no_unique_address]] TValue Value;
#endif
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
		static constexpr usize RESERVED_HASH_LIMIT = 1;

		struct Slot final
		{
			Slot() noexcept;

			Slot(const Slot&) noexcept
			    requires IsTriviallyCopyable<Entry>
			= default;
			Slot(Slot&&) noexcept
			    requires IsTriviallyCopyable<Entry>
			= default;

			~Slot() noexcept
			    requires IsTriviallyDestructible<Entry>
			= default;
			~Slot() noexcept
			    requires(!IsTriviallyDestructible<Entry>);

			// Only occupied slots have a cached hash; the control array owns slot state.
			// Touch reserved storage sequentially instead of faulting pages during hashed insertion.
			usize Hash = 0;

			union
			{
				Entry Item;
			};
		};
		static_assert(!IsTriviallyCopyable<Entry> || IsTriviallyCopyable<Slot>,
		              "Trivial entries must enable the whole-slot copy path.");

		struct InsertPosition
		{
			usize Index;
			bool Occupied;
		};

	public:
		template <bool TConst> class BasicIterator final
		{
			template <bool> friend class BasicIterator;
			using Table = core::Conditional<TConst, const HashMap, HashMap>;

		public:
			BasicIterator(Table& table, usize index) noexcept;

			using iterator_category = core::ForwardIteratorTag;
			using value_type = Entry;
			using difference_type = isize;
			using reference = core::Conditional<TConst, const Entry&, Entry&>;
			using pointer = core::Conditional<TConst, const Entry*, Entry*>;
			BasicIterator() noexcept = default;
			BasicIterator(const BasicIterator&) noexcept = default;
			BasicIterator& operator=(const BasicIterator&) noexcept = default;

			BasicIterator(const BasicIterator<false>& other) noexcept
			    requires TConst;

			[[nodiscard]] reference operator*() const noexcept;
			[[nodiscard]] pointer operator->() const noexcept;

			ROXELL_FORCE_INLINE BasicIterator& operator++() noexcept;
			BasicIterator operator++(int) noexcept;

			bool operator==(const BasicIterator& other) const noexcept;

		private:
			[[nodiscard]] static uint64 occupiedGroup(const uint8* controls) noexcept;

			ROXELL_FORCE_INLINE void skipScalar() noexcept;
			ROXELL_FORCE_INLINE void skipEmpty() noexcept;

		private:
			Table* m_Table = nullptr;
			usize m_Index = 0;
			uint64 m_RemainingGroup = 0;
		};

		using Iterator = BasicIterator<false>;
		using ConstIterator = BasicIterator<true>;

		explicit HashMap(MemoryResource& resource, const THash& hash = {}, const TEqual& equal = {}) noexcept;

		HashMap(const HashMap& other) noexcept
		    requires(IsNothrowCopyConstructible<TKey> && IsNothrowCopyConstructible<TValue>);

		HashMap(HashMap&& other) noexcept;

		~HashMap();

		HashMap& operator=(const HashMap& other) noexcept
		    requires(IsNothrowCopyConstructible<TKey> && IsNothrowCopyConstructible<TValue>);

		HashMap& operator=(HashMap&& other) noexcept;

		[[nodiscard]] usize Size() const noexcept;
		[[nodiscard]] usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		[[nodiscard]] Iterator begin() noexcept;
		[[nodiscard]] Iterator end() noexcept;

		[[nodiscard]] ConstIterator begin() const noexcept;
		[[nodiscard]] ConstIterator end() const noexcept;

		// Reserve counts live entries and may rehash to remove tombstones even without growth.
		void ReserveRequired(const usize minimumCapacity) noexcept;
		[[nodiscard]] bool Reserve(const usize minimumCapacity) noexcept;

		// Duplicate insertion neither constructs a mapped value nor consumes forwarded arguments.
		template <typename TKeyArgument, typename... TArguments>
		InsertResult EmplaceRequired(TKeyArgument&& key, TArguments&&... arguments) noexcept
		    requires(IsSame<RemoveCvref<TKeyArgument>, TKey> &&
		             IsNothrowConstructible<TKey, TKeyArgument &&> &&
		             IsNothrowConstructible<TValue, TArguments && ...>);

		// A null value reports slot allocation failure without consuming arguments or changing the table.
		// Element constructors retain their own allocation/failure contracts.
		template <typename TKeyArgument, typename... TArguments>
		[[nodiscard]] InsertResult Emplace(TKeyArgument&& key, TArguments&&... arguments) noexcept
		    requires requires { EmplaceRequired(core::Forward<TKeyArgument>(key), core::Forward<TArguments>(arguments)...); };

		template <typename TKeyArgument, typename TValueArgument>
		[[nodiscard]] InsertResult Insert(TKeyArgument&& key, TValueArgument&& value) noexcept
		    requires requires { Emplace(core::Forward<TKeyArgument>(key), core::Forward<TValueArgument>(value)); };

		template <typename TKeyArgument, typename TValueArgument>
		InsertResult InsertRequired(TKeyArgument&& key, TValueArgument&& value) noexcept
		    requires requires { EmplaceRequired(core::Forward<TKeyArgument>(key), core::Forward<TValueArgument>(value)); };

		template <typename TLookup = TKey>
		[[nodiscard]] TValue* Find(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] Entry* FindEntry(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] const Entry* FindEntry(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] const TValue* Find(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] bool Contains(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] TValue& Get(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] const TValue& Get(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		[[nodiscard]] TValue& operator[](const TKey& key) noexcept;
		[[nodiscard]] const TValue& operator[](const TKey& key) const noexcept;

		template <typename TLookup = TKey>
		bool Erase(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TPredicate>
		usize EraseIf(TPredicate&& predicate) noexcept
		    requires IsNothrowInvocableR<bool, TPredicate&, const Entry&>;

		void Clear() noexcept;
		void Reset() noexcept;

	private:
		static constexpr usize storageBytes(const usize buckets) noexcept;
		static uint8* controlBytes(Slot* slots, const usize buckets) noexcept;
		static const uint8* controlBytes(const Slot* slots, const usize buckets) noexcept;

		[[nodiscard]] Slot* tryAllocateSlots(const usize minimumCapacity,
		                                    usize& buckets) noexcept;

		static constexpr usize normalizeHash(const usize hash) noexcept;

		template <typename TLookup>
		[[nodiscard]] InsertPosition findInsertPosition(const TLookup& key, const usize hash) const noexcept;

		InsertResult insertPending(Entry& pending, const usize hash, const usize index) noexcept;

		template <typename TLookup>
		[[nodiscard]] usize findIndex(const TLookup& key, const usize hash) const noexcept;
		[[nodiscard]] usize findAvailable(const usize hash) const noexcept;

		void rehash(const usize bucketCount) noexcept;
		void relocate(Slot* const slots, const usize bucketCount) noexcept;

		void stealStorage(HashMap& other) noexcept;
		static void copySlotStorage(Slot* destination, const Slot* source, usize count) noexcept;
		static usize copyBucketCount(const HashMap& other) noexcept;
		void copyLiveEntries(const HashMap& other) noexcept;
		void copyFrom(const HashMap& other) noexcept;

	private:
		Slot* m_Slots = nullptr;
		usize m_BucketCount = 0;
		usize m_Size = 0;
		usize m_Deleted = 0;
		MemoryResource* m_Resource;
		THash m_Hash;
		TEqual m_Equal;
	};
}

namespace rox::core
{
	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TKeyArgument, typename... TArguments>
	HashMap<TKey, TValue, THash, TEqual>::Entry::Entry(TKeyArgument&& key, TArguments&&... arguments) noexcept
	    requires(IsNothrowConstructible<TKey, TKeyArgument &&> &&
	             IsNothrowConstructible<TValue, TArguments && ...>)
	    : m_Key(core::Forward<TKeyArgument>(key))
	    , Value(core::Forward<TArguments>(arguments)...)
	{
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	const TKey& HashMap<TKey, TValue, THash, TEqual>::Entry::GetKey() const noexcept
	{
		return m_Key;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	HashMap<TKey, TValue, THash, TEqual>::Slot::Slot() noexcept
	{
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	HashMap<TKey, TValue, THash, TEqual>::Slot::~Slot() noexcept
	    requires(!IsTriviallyDestructible<Entry>)
	{
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::BasicIterator(Table& table, usize index) noexcept
	    : m_Table(&table)
	    , m_Index(index)
	{
		ASSERT(index <= table.m_BucketCount, "HashMap iterator position must lie within its table.");
		skipEmpty();
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::BasicIterator(const BasicIterator<false>& other) noexcept
	    requires TConst
	    : m_Table(other.m_Table)
	    , m_Index(other.m_Index)
	    , m_RemainingGroup(other.m_RemainingGroup)
	{
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	typename HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::reference HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::operator*() const noexcept
	{
		ASSERT(m_Table != nullptr && m_Index < m_Table->m_BucketCount, "HashMap iterator must address a live entry.");
		ASSERT(controlBytes(m_Table->m_Slots, m_Table->m_BucketCount)[m_Index] > 1, "HashMap iterator entry must remain live.");
		return m_Table->m_Slots[m_Index].Item;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	typename HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::pointer HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::operator->() const noexcept
	{
		return &operator*();
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	ROXELL_FORCE_INLINE HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>& HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::operator++() noexcept
	{
		ASSERT(m_Table != nullptr && m_Index < m_Table->m_BucketCount, "HashMap cannot increment an end iterator.");
		if (m_Table->m_BucketCount < 32768)
		{
			++m_Index;
			skipScalar();
			return *this;
		}
		if (m_RemainingGroup != 0)
		{
			m_Index = (m_Index & ~usize{63}) + static_cast<usize>(core::CountTrailingZeros(m_RemainingGroup));
			m_RemainingGroup &= m_RemainingGroup - 1;
			return *this;
		}
		++m_Index;
		skipEmpty();
		return *this;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst> HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::operator++(int) noexcept
	{
		auto previous = *this;
		++*this;
		return previous;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	bool HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::operator==(const BasicIterator& other) const noexcept
	{
		return m_Table == other.m_Table && m_Index == other.m_Index;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	uint64 HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::occupiedGroup(const uint8* controls) noexcept
	{
		uint64 result = 0;
		for (usize offset = 0; offset < 64; offset += 8)
		{
			uint64 bytes = 0;
			core::CopyBytes(&bytes, controls + offset, sizeof(bytes));
			const uint64 bits = ((bytes & uint64{0x8080808080808080}) *
			                              uint64{0x0002040810204081}) >> 56;
			result |= bits << offset;
		}
		return result;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	ROXELL_FORCE_INLINE void HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::skipScalar() noexcept
	{
		const usize bucketCount = m_Table->m_BucketCount;
		usize index = m_Index;
		if (index != bucketCount)
		{
			const uint8* const controls = controlBytes(m_Table->m_Slots, bucketCount);
			while (index < bucketCount && controls[index] <= 1)
			{
				++index;
			}
		}
		m_Index = index;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	ROXELL_FORCE_INLINE void HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::skipEmpty() noexcept
	{
		const usize bucketCount = m_Table->m_BucketCount;
		usize index = m_Index;
		if (index == bucketCount)
		{
			return;
		}
		const uint8* const controls = controlBytes(m_Table->m_Slots, bucketCount);
		if constexpr (core::ByteOrder::native == core::ByteOrder::little)
		{
			// Scalar scans avoid mask bookkeeping for small control arrays.
			if (bucketCount >= 32768)
			{
				// Large bucket counts are multiples of 64; pack their control high bits into one mask.
				usize group = index & ~usize{63};
				uint64 occupied = occupiedGroup(controls + group);
				occupied &= ~uint64{0} << (index - group);
				while (occupied == 0)
				{
					group += 64;
					if (group == bucketCount)
					{
						m_Index = bucketCount;
						return;
					}
					occupied = occupiedGroup(controls + group);
				}
				m_Index = group + static_cast<usize>(core::CountTrailingZeros(occupied));
				m_RemainingGroup = occupied & (occupied - 1);
				return;
			}
		}
		while (index < bucketCount && controls[index] <= 1)
		{
			++index;
		}
		m_Index = index;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	HashMap<TKey, TValue, THash, TEqual>::HashMap(MemoryResource& resource, const THash& hash, const TEqual& equal) noexcept
	    : m_Resource(&resource)
	    , m_Hash(hash)
	    , m_Equal(equal)
	{
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	HashMap<TKey, TValue, THash, TEqual>::HashMap(const HashMap& other) noexcept
	    requires(IsNothrowCopyConstructible<TKey> && IsNothrowCopyConstructible<TValue>)
	    : HashMap(*other.m_Resource, other.m_Hash, other.m_Equal)
	{
		copyFrom(other);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	HashMap<TKey, TValue, THash, TEqual>::HashMap(HashMap&& other) noexcept
	    : HashMap(*other.m_Resource, other.m_Hash, other.m_Equal)
	{
		stealStorage(other);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	HashMap<TKey, TValue, THash, TEqual>::~HashMap()
	{
		Reset();
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	HashMap<TKey, TValue, THash, TEqual>& HashMap<TKey, TValue, THash, TEqual>::operator=(const HashMap& other) noexcept
	    requires(IsNothrowCopyConstructible<TKey> && IsNothrowCopyConstructible<TValue>)
	{
		if (this != &other)
		{
			if constexpr (IsTriviallyCopyable<Slot>)
			{
				if (other.m_Size != 0 && m_BucketCount == copyBucketCount(other))
				{
					m_Hash = other.m_Hash;
					m_Equal = other.m_Equal;
					if (m_BucketCount == other.m_BucketCount)
					{
						copySlotStorage(m_Slots, other.m_Slots, m_BucketCount);
						m_Deleted = other.m_Deleted;
					}
					else
					{
						core::SetBytes(controlBytes(m_Slots, m_BucketCount), 0, m_BucketCount);
						copyLiveEntries(other);
						m_Deleted = 0;
					}
					m_Size = other.m_Size;
					return *this;
				}
			}
			HashMap replacement(*m_Resource, other.m_Hash, other.m_Equal);
			replacement.copyFrom(other);
			*this = core::Move(replacement);
		}

		return *this;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	HashMap<TKey, TValue, THash, TEqual>& HashMap<TKey, TValue, THash, TEqual>::operator=(HashMap&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}
		Reset();
		m_Hash = other.m_Hash;
		m_Equal = other.m_Equal;

		if (m_Resource == other.m_Resource)
		{
			stealStorage(other);
		}
		else
		{
			ReserveRequired(other.m_Size);

			for (auto& entry : other)
			{
				EmplaceRequired(core::Move(entry.m_Key), core::Move(entry.Value));
			}
			other.Reset();
		}

		return *this;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	usize HashMap<TKey, TValue, THash, TEqual>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	usize HashMap<TKey, TValue, THash, TEqual>::Capacity() const noexcept
	{
		return m_BucketCount - m_BucketCount / 4;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	bool HashMap<TKey, TValue, THash, TEqual>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	MemoryResource& HashMap<TKey, TValue, THash, TEqual>::GetResource() const noexcept
	{
		return *m_Resource;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	typename HashMap<TKey, TValue, THash, TEqual>::Iterator HashMap<TKey, TValue, THash, TEqual>::begin() noexcept
	{
		return {*this, 0};
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	typename HashMap<TKey, TValue, THash, TEqual>::Iterator HashMap<TKey, TValue, THash, TEqual>::end() noexcept
	{
		return {*this, m_BucketCount};
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	typename HashMap<TKey, TValue, THash, TEqual>::ConstIterator HashMap<TKey, TValue, THash, TEqual>::begin() const noexcept
	{
		return {*this, 0};
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	typename HashMap<TKey, TValue, THash, TEqual>::ConstIterator HashMap<TKey, TValue, THash, TEqual>::end() const noexcept
	{
		return {*this, m_BucketCount};
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::ReserveRequired(const usize minimumCapacity) noexcept
	{
		if (minimumCapacity <= Capacity() && m_Deleted == 0)
		{
			return;
		}

		usize buckets = m_BucketCount == 0 ? 8 : m_BucketCount;
		constexpr usize maximumBuckets = NumericLimits<usize>::max() / (sizeof(Slot) + sizeof(uint8));

		while (buckets - buckets / 4 < minimumCapacity)
		{
			ASSERT(buckets <= maximumBuckets / 2, "HashMap capacity must fit addressable slot storage.");
			buckets *= 2;
		}
		ASSERT(buckets <= maximumBuckets, "HashMap slot allocation size must not overflow.");
		rehash(buckets);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	bool HashMap<TKey, TValue, THash, TEqual>::Reserve(const usize minimumCapacity) noexcept
	{
		if (minimumCapacity <= Capacity() && m_Deleted == 0)
		{
			return true;
		}
		usize buckets = 0;
		Slot* const slots = tryAllocateSlots(minimumCapacity, buckets);
		if (slots == nullptr)
		{
			return false;
		}
		relocate(slots, buckets);
		return true;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TKeyArgument, typename... TArguments>
	typename HashMap<TKey, TValue, THash, TEqual>::InsertResult HashMap<TKey, TValue, THash, TEqual>::EmplaceRequired(TKeyArgument&& key, TArguments&&... arguments) noexcept
	    requires(IsSame<RemoveCvref<TKeyArgument>, TKey> &&
	             IsNothrowConstructible<TKey, TKeyArgument &&> &&
	             IsNothrowConstructible<TValue, TArguments && ...>)
	{
		const usize hash = normalizeHash(static_cast<usize>(core::AsConst(m_Hash)(core::AsConst(key))));
		InsertPosition position = findInsertPosition(key, hash);

		if (position.Occupied)
		{
			return {core::AddressOf(m_Slots[position.Index].Item.Value), false};
		}
		Entry pending(core::Forward<TKeyArgument>(key), core::Forward<TArguments>(arguments)...);

		const usize addedOccupancy = m_BucketCount != 0 && controlBytes(m_Slots, m_BucketCount)[position.Index] == 1 ? 0 : 1;
		if (m_Size + m_Deleted + addedOccupancy > Capacity())
		{
			ReserveRequired(m_Size + 1);
			position.Index = findAvailable(hash);
		}

		return insertPending(pending, hash, position.Index);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TKeyArgument, typename... TArguments>
	typename HashMap<TKey, TValue, THash, TEqual>::InsertResult HashMap<TKey, TValue, THash, TEqual>::Emplace(TKeyArgument&& key, TArguments&&... arguments) noexcept
	    requires requires { EmplaceRequired(core::Forward<TKeyArgument>(key), core::Forward<TArguments>(arguments)...); }
	{
		const usize hash = normalizeHash(static_cast<usize>(core::AsConst(m_Hash)(core::AsConst(key))));
		InsertPosition position = findInsertPosition(key, hash);

		if (position.Occupied)
		{
			return {core::AddressOf(m_Slots[position.Index].Item.Value), false};
		}

		usize buckets = 0;
		Slot* slots = nullptr;

		const usize addedOccupancy = m_BucketCount != 0 && controlBytes(m_Slots, m_BucketCount)[position.Index] == 1 ? 0 : 1;
		if (m_Size + m_Deleted + addedOccupancy > Capacity())
		{
			slots = tryAllocateSlots(m_Size + 1, buckets);

			if (slots == nullptr)
			{
				return {nullptr, false};
			}
		}

		Entry pending(core::Forward<TKeyArgument>(key), core::Forward<TArguments>(arguments)...);

		if (slots != nullptr)
		{
			relocate(slots, buckets);
			position.Index = findAvailable(hash);
		}

		return insertPending(pending, hash, position.Index);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TKeyArgument, typename TValueArgument>
	typename HashMap<TKey, TValue, THash, TEqual>::InsertResult HashMap<TKey, TValue, THash, TEqual>::Insert(TKeyArgument&& key, TValueArgument&& value) noexcept
	    requires requires { Emplace(core::Forward<TKeyArgument>(key), core::Forward<TValueArgument>(value)); }
	{
		return Emplace(core::Forward<TKeyArgument>(key), core::Forward<TValueArgument>(value));
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TKeyArgument, typename TValueArgument>
	typename HashMap<TKey, TValue, THash, TEqual>::InsertResult HashMap<TKey, TValue, THash, TEqual>::InsertRequired(TKeyArgument&& key, TValueArgument&& value) noexcept
	    requires requires { EmplaceRequired(core::Forward<TKeyArgument>(key), core::Forward<TValueArgument>(value)); }
	{
		return EmplaceRequired(core::Forward<TKeyArgument>(key), core::Forward<TValueArgument>(value));
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	TValue* HashMap<TKey, TValue, THash, TEqual>::Find(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		auto* entry = FindEntry(key);
		return entry == nullptr ? nullptr : core::AddressOf(entry->Value);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	typename HashMap<TKey, TValue, THash, TEqual>::Entry* HashMap<TKey, TValue, THash, TEqual>::FindEntry(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key, normalizeHash(static_cast<usize>(core::AsConst(m_Hash)(key))));
		return index == m_BucketCount ? nullptr : &m_Slots[index].Item;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	const typename HashMap<TKey, TValue, THash, TEqual>::Entry* HashMap<TKey, TValue, THash, TEqual>::FindEntry(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key, normalizeHash(static_cast<usize>(core::AsConst(m_Hash)(key))));
		return index == m_BucketCount ? nullptr : &m_Slots[index].Item;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	const TValue* HashMap<TKey, TValue, THash, TEqual>::Find(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto* entry = FindEntry(key);
		return entry == nullptr ? nullptr : core::AddressOf(entry->Value);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	bool HashMap<TKey, TValue, THash, TEqual>::Contains(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		return Find(key) != nullptr;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	TValue& HashMap<TKey, TValue, THash, TEqual>::Get(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		auto* value = Find(key);
		ASSERT(value != nullptr, "HashMap::Get requires an existing key.");
		return *value;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	const TValue& HashMap<TKey, TValue, THash, TEqual>::Get(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto* value = Find(key);
		ASSERT(value != nullptr, "HashMap::Get requires an existing key.");
		return *value;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	TValue& HashMap<TKey, TValue, THash, TEqual>::operator[](const TKey& key) noexcept
	{
		return Get(key);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	const TValue& HashMap<TKey, TValue, THash, TEqual>::operator[](const TKey& key) const noexcept
	{
		return Get(key);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	bool HashMap<TKey, TValue, THash, TEqual>::Erase(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key, normalizeHash(static_cast<usize>(core::AsConst(m_Hash)(key))));

		if (index == m_BucketCount)
		{
			return false;
		}

		core::DestroyAt(&m_Slots[index].Item);
		controlBytes(m_Slots, m_BucketCount)[index] = 1;
		--m_Size;
		++m_Deleted;
		return true;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TPredicate>
	usize HashMap<TKey, TValue, THash, TEqual>::EraseIf(TPredicate&& predicate) noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const Entry&>
	{
		usize erased = 0;

		for (usize index = 0; index < m_BucketCount; ++index)
		{
			Slot& slot = m_Slots[index];

			if (controlBytes(m_Slots, m_BucketCount)[index] <= 1 || !core::Invoke(predicate, core::AsConst(slot.Item)))
			{
				continue;
			}

			core::DestroyAt(&slot.Item);
			controlBytes(m_Slots, m_BucketCount)[index] = 1;
			--m_Size;
			++m_Deleted;
			++erased;
		}

		return erased;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::Clear() noexcept
	{
		Slot* const slots = m_Slots;
		const usize bucketCount = m_BucketCount;
		uint8* const controls = bucketCount != 0 ? controlBytes(slots, bucketCount) : nullptr;
		if constexpr (!IsTriviallyDestructible<Entry>)
		{
			for (usize index = 0; index < bucketCount; ++index)
			{
				if (controls[index] > 1)
				{
					core::DestroyAt(&slots[index].Item);
					controls[index] = 0;
				}
			}
		}
		if (bucketCount != 0)
		{
			core::SetBytes(controls, 0, bucketCount);
		}

		m_Size = 0;
		m_Deleted = 0;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::Reset() noexcept
	{
		if constexpr (!IsTriviallyDestructible<Entry>)
		{
			Clear();
		}
		m_Size = 0;
		m_Deleted = 0;

		if (m_Slots != nullptr)
		{
			core::DestroyN(m_Slots, m_BucketCount);
			m_Resource->Deallocate(m_Slots, {storageBytes(m_BucketCount), alignof(Slot)});
		}

		m_Slots = nullptr;
		m_BucketCount = 0;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	constexpr usize HashMap<TKey, TValue, THash, TEqual>::storageBytes(const usize buckets) noexcept
	{
		return buckets * (sizeof(Slot) + sizeof(uint8));
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	uint8* HashMap<TKey, TValue, THash, TEqual>::controlBytes(Slot* slots, const usize buckets) noexcept
	{
		return reinterpret_cast<uint8*>(slots + buckets);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	const uint8* HashMap<TKey, TValue, THash, TEqual>::controlBytes(const Slot* slots, const usize buckets) noexcept
	{
		return reinterpret_cast<const uint8*>(slots + buckets);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	typename HashMap<TKey, TValue, THash, TEqual>::Slot* HashMap<TKey, TValue, THash, TEqual>::tryAllocateSlots(const usize minimumCapacity,
	                                    usize& buckets) noexcept
	{
		buckets = m_BucketCount == 0 ? 8 : m_BucketCount;
		constexpr usize maximumBuckets = NumericLimits<usize>::max() / (sizeof(Slot) + sizeof(uint8));

		while (buckets - buckets / 4 < minimumCapacity)
		{
			if (buckets > maximumBuckets / 2)
			{
				return nullptr;
			}
			buckets *= 2;
		}

		if (buckets > maximumBuckets)
		{
			return nullptr;
		}

		return static_cast<Slot*>(m_Resource->Allocate({storageBytes(buckets), alignof(Slot)}));
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	constexpr usize HashMap<TKey, TValue, THash, TEqual>::normalizeHash(const usize hash) noexcept
	{
		return hash <= RESERVED_HASH_LIMIT ? hash + 2 : hash;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	typename HashMap<TKey, TValue, THash, TEqual>::InsertPosition HashMap<TKey, TValue, THash, TEqual>::findInsertPosition(const TLookup& key, const usize hash) const noexcept
	{
		if (m_BucketCount == 0)
		{
			return {0, false};
		}
		const uint8* const controls = controlBytes(m_Slots, m_BucketCount);
		usize available = m_BucketCount;
		usize index = hash & (m_BucketCount - 1);
		while (controls[index] != 0)
		{
			const Slot& slot = m_Slots[index];
			if (controls[index] == 1)
			{
				if (available == m_BucketCount)
				{
					available = index;
				}
			}
			else if (slot.Hash == hash && m_Equal(slot.Item.m_Key, key))
			{
				return {index, true};
			}
			index = (index + 1) & (m_BucketCount - 1);
		}
		return {available == m_BucketCount ? index : available, false};
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	typename HashMap<TKey, TValue, THash, TEqual>::InsertResult HashMap<TKey, TValue, THash, TEqual>::insertPending(Entry& pending, const usize hash, const usize index) noexcept
	{
		Slot& slot = m_Slots[index];

		if (controlBytes(m_Slots, m_BucketCount)[index] == 1)
		{
			--m_Deleted;
		}

		core::ConstructAt(&slot.Item, core::Move(pending.m_Key), core::Move(pending.Value));
		slot.Hash = hash;
		controlBytes(m_Slots, m_BucketCount)[index] = 0x80;
		++m_Size;
		return {core::AddressOf(slot.Item.Value), true};
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	usize HashMap<TKey, TValue, THash, TEqual>::findIndex(const TLookup& key, const usize hash) const noexcept
	{
		if (m_Size == 0)
		{
			return m_BucketCount;
		}

		const usize bucketCount = m_BucketCount;
		const Slot* const slots = m_Slots;
		const uint8* const controls = controlBytes(slots, bucketCount);
		usize index = hash & (bucketCount - 1);

		for (;;)
		{
			const Slot& slot = slots[index];
			const uint8 state = controls[index];
			// Hash storage remains initialized even when its entry has been destroyed.
			if (slot.Hash == hash && state > 1 && m_Equal(slot.Item.m_Key, key))
			{
				return index;
			}
			if (state == 0)
			{
				return bucketCount;
			}
			index = (index + 1) & (bucketCount - 1);
		}
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	usize HashMap<TKey, TValue, THash, TEqual>::findAvailable(const usize hash) const noexcept
	{
		const uint8* const controls = controlBytes(m_Slots, m_BucketCount);
		usize index = hash & (m_BucketCount - 1);

		while (controls[index] > 1)
		{
			index = (index + 1) & (m_BucketCount - 1);
		}

		return index;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::rehash(const usize bucketCount) noexcept
	{
		Slot* const slots = static_cast<Slot*>(m_Resource->AllocateRequired({storageBytes(bucketCount), alignof(Slot)}));
		relocate(slots, bucketCount);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::relocate(Slot* const slots, const usize bucketCount) noexcept
	{
		Slot* oldSlots = m_Slots;
		const usize oldCount = m_BucketCount;

		m_Slots = slots;
		m_BucketCount = bucketCount;
		core::SetBytes(controlBytes(m_Slots, m_BucketCount), 0, m_BucketCount);

		for (usize index = 0; index < bucketCount; ++index)
		{
			core::ConstructAt(m_Slots + index);
		}

		for (usize index = 0; index < oldCount; ++index)
		{
			Slot& previous = oldSlots[index];

			if (controlBytes(oldSlots, oldCount)[index] > 1)
			{
				const usize destinationIndex = findAvailable(previous.Hash);
				Slot& destination = m_Slots[destinationIndex];
				core::ConstructAt(&destination.Item, core::Move(previous.Item.m_Key),
				                  core::Move(previous.Item.Value));
				destination.Hash = previous.Hash;
				controlBytes(m_Slots, m_BucketCount)[destinationIndex] = 0x80;
				core::DestroyAt(&previous.Item);
			}

			core::DestroyAt(&previous);
		}

		if (oldSlots != nullptr)
		{
			m_Resource->Deallocate(oldSlots, {storageBytes(oldCount), alignof(Slot)});
		}

		m_Deleted = 0;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::stealStorage(HashMap& other) noexcept
	{
		m_Slots = core::Exchange(other.m_Slots, nullptr);
		m_BucketCount = core::Exchange(other.m_BucketCount, 0);
		m_Size = core::Exchange(other.m_Size, 0);
		m_Deleted = core::Exchange(other.m_Deleted, 0);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::copySlotStorage(Slot* destination, const Slot* source, usize count) noexcept
	{
		core::CopyBytes(controlBytes(destination, count), controlBytes(source, count), count);
		// Bound streaming-copy sizes for tables that will be probed immediately.
		// Copy whole slots so each implicit-lifetime object fits within one copy.
		constexpr usize BLOCK_SLOTS = sizeof(Slot) < 65536 ? 65536 / sizeof(Slot) : 1;
		while (count > BLOCK_SLOTS)
		{
			core::CopyBytes(destination, source, BLOCK_SLOTS * sizeof(Slot));
			destination += BLOCK_SLOTS;
			source += BLOCK_SLOTS;
			count -= BLOCK_SLOTS;
		}
		core::CopyBytes(destination, source, count * sizeof(Slot));
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	usize HashMap<TKey, TValue, THash, TEqual>::copyBucketCount(const HashMap& other) noexcept
	{
		usize compactBuckets = other.m_BucketCount;
		while (compactBuckets > 8 && compactBuckets / 2 - compactBuckets / 8 >= other.m_Size)
		{
			compactBuckets /= 2;
		}
		// Dense trivial copies avoid rebuilding probe chains. Sparse copies avoid
		// allocating and writing a large amount of unused entry storage.
		return compactBuckets <= other.m_BucketCount / 4 ? compactBuckets : other.m_BucketCount;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::copyLiveEntries(const HashMap& other) noexcept
	{
		const uint8* const sourceControls = controlBytes(other.m_Slots, other.m_BucketCount);
		uint8* const destinationControls = controlBytes(m_Slots, m_BucketCount);
		for (usize index = 0; index < other.m_BucketCount; ++index)
		{
			if (sourceControls[index] > 1)
			{
				const Slot& source = other.m_Slots[index];
				const usize destinationIndex = findAvailable(source.Hash);
				Slot& destination = m_Slots[destinationIndex];
				core::ConstructAt(&destination.Item, source.Item.m_Key, source.Item.Value);
				destination.Hash = source.Hash;
				destinationControls[destinationIndex] = 0x80;
			}
		}
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::copyFrom(const HashMap& other) noexcept
	{
		if (other.m_Size == 0)
		{
			return;
		}
		m_BucketCount = copyBucketCount(other);
		m_Slots = static_cast<Slot*>(m_Resource->AllocateRequired({storageBytes(m_BucketCount), alignof(Slot)}));
		if (m_BucketCount != other.m_BucketCount)
		{
			core::SetBytes(controlBytes(m_Slots, m_BucketCount), 0, m_BucketCount);
			for (usize index = 0; index < m_BucketCount; ++index)
			{
				core::ConstructAt(m_Slots + index);
			}
			copyLiveEntries(other);
			m_Size = other.m_Size;
			return;
		}
		if constexpr (IsTriviallyCopyable<Slot>)
		{
			// Trivial eligible Slot/Entry constructors and destructors make these
			// implicit-lifetime objects. CopyBytes starts their destination lifetimes;
			// inactive union storage is copied only as object representation.
			copySlotStorage(m_Slots, other.m_Slots, m_BucketCount);
		}
		else
		{
			Slot* const destinationSlots = m_Slots;
			const Slot* const sourceSlots = other.m_Slots;
			const usize bucketCount = m_BucketCount;
			const uint8* const sourceControls = controlBytes(sourceSlots, bucketCount);
			for (usize index = 0; index < bucketCount; ++index)
			{
				Slot& destination = *core::ConstructAt(destinationSlots + index);
				const Slot& source = sourceSlots[index];
				if (sourceControls[index] > 1)
				{
					core::ConstructAt(&destination.Item, source.Item.m_Key, source.Item.Value);
					destination.Hash = source.Hash;
				}
			}
			core::CopyBytes(controlBytes(destinationSlots, bucketCount), sourceControls, bucketCount);
		}
		m_Size = other.m_Size;
		m_Deleted = other.m_Deleted;
	}
}
