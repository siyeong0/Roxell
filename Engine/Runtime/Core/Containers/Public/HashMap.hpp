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
		static_assert(rox::core::IsNothrowMoveConstructible<TKey> && rox::core::IsNothrowDestructible<TKey>);
		static_assert(rox::core::IsNothrowMoveConstructible<TValue> && rox::core::IsNothrowDestructible<TValue>);
		static_assert(rox::core::IsNothrowCopyConstructible<THash> && rox::core::IsNothrowCopyAssignable<THash>);
		static_assert(rox::core::IsNothrowCopyConstructible<TEqual> && rox::core::IsNothrowCopyAssignable<TEqual>);
		static_assert(rox::core::IsNothrowDestructible<THash> && rox::core::IsNothrowDestructible<TEqual>);
		static_assert(rox::core::IsNothrowInvocableR<rox::core::usize, const THash&, const TKey&>);
		static_assert(rox::core::IsNothrowInvocableR<bool, const TEqual&, const TKey&, const TKey&>);
		template <typename TLookup>
		static constexpr bool SUPPORTS_LOOKUP = rox::core::IsNothrowInvocableR<rox::core::usize, const THash&, const TLookup&> && rox::core::IsNothrowInvocableR<bool, const TEqual&, const TKey&, const TLookup&>;

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
			    requires(rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> &&
			             rox::core::IsNothrowConstructible<TValue, TArguments && ...>);

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
		static constexpr rox::core::usize RESERVED_HASH_LIMIT = 1;

		struct Slot final
		{
			Slot() noexcept;

			Slot(const Slot&) noexcept
			    requires rox::core::IsTriviallyCopyable<Entry>
			= default;
			Slot(Slot&&) noexcept
			    requires rox::core::IsTriviallyCopyable<Entry>
			= default;

			~Slot() noexcept
			    requires rox::core::IsTriviallyDestructible<Entry>
			= default;
			~Slot() noexcept
			    requires(!rox::core::IsTriviallyDestructible<Entry>);

			// Only occupied slots have a cached hash; the control array owns slot state.
			// Touch reserved storage sequentially instead of faulting pages during hashed insertion.
			rox::core::usize Hash = 0;

			union
			{
				Entry Item;
			};
		};
		static_assert(!rox::core::IsTriviallyCopyable<Entry> || rox::core::IsTriviallyCopyable<Slot>,
		              "Trivial entries must enable the whole-slot copy path.");

		struct InsertPosition
		{
			rox::core::usize Index;
			bool Occupied;
		};

	public:
		template <bool TConst> class BasicIterator final
		{
			template <bool> friend class BasicIterator;
			using Table = rox::core::Conditional<TConst, const HashMap, HashMap>;

		public:
			BasicIterator(Table& table, rox::core::usize index) noexcept;

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

			ROXELL_FORCE_INLINE BasicIterator& operator++() noexcept;
			BasicIterator operator++(int) noexcept;

			bool operator==(const BasicIterator& other) const noexcept;

		private:
			[[nodiscard]] static rox::core::uint64 occupiedGroup(const rox::core::uint8* controls) noexcept;

			ROXELL_FORCE_INLINE void skipScalar() noexcept;
			ROXELL_FORCE_INLINE void skipEmpty() noexcept;

		private:
			Table* m_Table = nullptr;
			rox::core::usize m_Index = 0;
			rox::core::uint64 m_RemainingGroup = 0;
		};

		using Iterator = BasicIterator<false>;
		using ConstIterator = BasicIterator<true>;

		explicit HashMap(MemoryResource& resource, const THash& hash = {}, const TEqual& equal = {}) noexcept;

		HashMap(const HashMap& other) noexcept
		    requires(rox::core::IsNothrowCopyConstructible<TKey> && rox::core::IsNothrowCopyConstructible<TValue>);

		HashMap(HashMap&& other) noexcept;

		~HashMap();

		HashMap& operator=(const HashMap& other) noexcept
		    requires(rox::core::IsNothrowCopyConstructible<TKey> && rox::core::IsNothrowCopyConstructible<TValue>);

		HashMap& operator=(HashMap&& other) noexcept;

		[[nodiscard]] rox::core::usize Size() const noexcept;
		[[nodiscard]] rox::core::usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		[[nodiscard]] Iterator begin() noexcept;
		[[nodiscard]] Iterator end() noexcept;

		[[nodiscard]] ConstIterator begin() const noexcept;
		[[nodiscard]] ConstIterator end() const noexcept;

		// Reserve counts live entries and may rehash to remove tombstones even without growth.
		void ReserveRequired(const rox::core::usize minimumCapacity) noexcept;
		[[nodiscard]] bool Reserve(const rox::core::usize minimumCapacity) noexcept;

		// Duplicate insertion neither constructs a mapped value nor consumes forwarded arguments.
		template <typename TKeyArgument, typename... TArguments>
		InsertResult EmplaceRequired(TKeyArgument&& key, TArguments&&... arguments) noexcept
		    requires(rox::core::IsSame<rox::core::RemoveCvref<TKeyArgument>, TKey> &&
		             rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> &&
		             rox::core::IsNothrowConstructible<TValue, TArguments && ...>);

		// A null value reports slot allocation failure without consuming arguments or changing the table.
		// Element constructors retain their own allocation/failure contracts.
		template <typename TKeyArgument, typename... TArguments>
		[[nodiscard]] InsertResult Emplace(TKeyArgument&& key, TArguments&&... arguments) noexcept
		    requires requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TArguments>(arguments)...); };

		template <typename TKeyArgument, typename TValueArgument>
		[[nodiscard]] InsertResult Insert(TKeyArgument&& key, TValueArgument&& value) noexcept
		    requires requires { Emplace(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); };

		template <typename TKeyArgument, typename TValueArgument>
		InsertResult InsertRequired(TKeyArgument&& key, TValueArgument&& value) noexcept
		    requires requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); };

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
		rox::core::usize EraseIf(TPredicate&& predicate) noexcept
		    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const Entry&>;

		void Clear() noexcept;
		void Reset() noexcept;

	private:
		static constexpr rox::core::usize storageBytes(const rox::core::usize buckets) noexcept;
		static rox::core::uint8* controlBytes(Slot* slots, const rox::core::usize buckets) noexcept;
		static const rox::core::uint8* controlBytes(const Slot* slots, const rox::core::usize buckets) noexcept;

		[[nodiscard]] Slot* tryAllocateSlots(const rox::core::usize minimumCapacity,
		                                    rox::core::usize& buckets) noexcept;

		static constexpr rox::core::usize normalizeHash(const rox::core::usize hash) noexcept;

		template <typename TLookup>
		[[nodiscard]] InsertPosition findInsertPosition(const TLookup& key, const rox::core::usize hash) const noexcept;

		InsertResult insertPending(Entry& pending, const rox::core::usize hash, const rox::core::usize index) noexcept;

		template <typename TLookup>
		[[nodiscard]] rox::core::usize findIndex(const TLookup& key, const rox::core::usize hash) const noexcept;
		[[nodiscard]] rox::core::usize findAvailable(const rox::core::usize hash) const noexcept;

		void rehash(const rox::core::usize bucketCount) noexcept;
		void relocate(Slot* const slots, const rox::core::usize bucketCount) noexcept;

		void stealStorage(HashMap& other) noexcept;
		static void copySlotStorage(Slot* destination, const Slot* source, rox::core::usize count) noexcept;
		static rox::core::usize copyBucketCount(const HashMap& other) noexcept;
		void copyLiveEntries(const HashMap& other) noexcept;
		void copyFrom(const HashMap& other) noexcept;

	private:
		Slot* m_Slots = nullptr;
		rox::core::usize m_BucketCount = 0;
		rox::core::usize m_Size = 0;
		rox::core::usize m_Deleted = 0;
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
	    requires(rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> &&
	             rox::core::IsNothrowConstructible<TValue, TArguments && ...>)
	    : m_Key(rox::core::Forward<TKeyArgument>(key))
	    , Value(rox::core::Forward<TArguments>(arguments)...)
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
	    requires(!rox::core::IsTriviallyDestructible<Entry>)
	{
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::BasicIterator(Table& table, rox::core::usize index) noexcept
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
			m_Index = (m_Index & ~rox::core::usize{63}) + static_cast<rox::core::usize>(rox::core::CountTrailingZeros(m_RemainingGroup));
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
	rox::core::uint64 HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::occupiedGroup(const rox::core::uint8* controls) noexcept
	{
		rox::core::uint64 result = 0;
		for (rox::core::usize offset = 0; offset < 64; offset += 8)
		{
			rox::core::uint64 bytes = 0;
			rox::core::CopyBytes(&bytes, controls + offset, sizeof(bytes));
			const rox::core::uint64 bits = ((bytes & rox::core::uint64{0x8080808080808080}) *
			                              rox::core::uint64{0x0002040810204081}) >> 56;
			result |= bits << offset;
		}
		return result;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <bool TConst>
	ROXELL_FORCE_INLINE void HashMap<TKey, TValue, THash, TEqual>::BasicIterator<TConst>::skipScalar() noexcept
	{
		const rox::core::usize bucketCount = m_Table->m_BucketCount;
		rox::core::usize index = m_Index;
		if (index != bucketCount)
		{
			const rox::core::uint8* const controls = controlBytes(m_Table->m_Slots, bucketCount);
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
		const rox::core::usize bucketCount = m_Table->m_BucketCount;
		rox::core::usize index = m_Index;
		if (index == bucketCount)
		{
			return;
		}
		const rox::core::uint8* const controls = controlBytes(m_Table->m_Slots, bucketCount);
		if constexpr (rox::core::ByteOrder::native == rox::core::ByteOrder::little)
		{
			// Scalar scans avoid mask bookkeeping for small control arrays.
			if (bucketCount >= 32768)
			{
				// Large bucket counts are multiples of 64; pack their control high bits into one mask.
				rox::core::usize group = index & ~rox::core::usize{63};
				rox::core::uint64 occupied = occupiedGroup(controls + group);
				occupied &= ~rox::core::uint64{0} << (index - group);
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
				m_Index = group + static_cast<rox::core::usize>(rox::core::CountTrailingZeros(occupied));
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
	    requires(rox::core::IsNothrowCopyConstructible<TKey> && rox::core::IsNothrowCopyConstructible<TValue>)
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
	    requires(rox::core::IsNothrowCopyConstructible<TKey> && rox::core::IsNothrowCopyConstructible<TValue>)
	{
		if (this != &other)
		{
			if constexpr (rox::core::IsTriviallyCopyable<Slot>)
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
						rox::core::SetBytes(controlBytes(m_Slots, m_BucketCount), 0, m_BucketCount);
						copyLiveEntries(other);
						m_Deleted = 0;
					}
					m_Size = other.m_Size;
					return *this;
				}
			}
			HashMap replacement(*m_Resource, other.m_Hash, other.m_Equal);
			replacement.copyFrom(other);
			*this = rox::core::Move(replacement);
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
				EmplaceRequired(rox::core::Move(entry.m_Key), rox::core::Move(entry.Value));
			}
			other.Reset();
		}

		return *this;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	rox::core::usize HashMap<TKey, TValue, THash, TEqual>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	rox::core::usize HashMap<TKey, TValue, THash, TEqual>::Capacity() const noexcept
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
	void HashMap<TKey, TValue, THash, TEqual>::ReserveRequired(const rox::core::usize minimumCapacity) noexcept
	{
		if (minimumCapacity <= Capacity() && m_Deleted == 0)
		{
			return;
		}

		rox::core::usize buckets = m_BucketCount == 0 ? 8 : m_BucketCount;
		constexpr rox::core::usize maximumBuckets = rox::core::NumericLimits<rox::core::usize>::max() / (sizeof(Slot) + sizeof(rox::core::uint8));

		while (buckets - buckets / 4 < minimumCapacity)
		{
			ASSERT(buckets <= maximumBuckets / 2, "HashMap capacity must fit addressable slot storage.");
			buckets *= 2;
		}
		ASSERT(buckets <= maximumBuckets, "HashMap slot allocation size must not overflow.");
		rehash(buckets);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	bool HashMap<TKey, TValue, THash, TEqual>::Reserve(const rox::core::usize minimumCapacity) noexcept
	{
		if (minimumCapacity <= Capacity() && m_Deleted == 0)
		{
			return true;
		}
		rox::core::usize buckets = 0;
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
	    requires(rox::core::IsSame<rox::core::RemoveCvref<TKeyArgument>, TKey> &&
	             rox::core::IsNothrowConstructible<TKey, TKeyArgument &&> &&
	             rox::core::IsNothrowConstructible<TValue, TArguments && ...>)
	{
		const rox::core::usize hash = normalizeHash(static_cast<rox::core::usize>(rox::core::AsConst(m_Hash)(rox::core::AsConst(key))));
		InsertPosition position = findInsertPosition(key, hash);

		if (position.Occupied)
		{
			return {rox::core::AddressOf(m_Slots[position.Index].Item.Value), false};
		}
		Entry pending(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TArguments>(arguments)...);

		const rox::core::usize addedOccupancy = m_BucketCount != 0 && controlBytes(m_Slots, m_BucketCount)[position.Index] == 1 ? 0 : 1;
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
	    requires requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TArguments>(arguments)...); }
	{
		const rox::core::usize hash = normalizeHash(static_cast<rox::core::usize>(rox::core::AsConst(m_Hash)(rox::core::AsConst(key))));
		InsertPosition position = findInsertPosition(key, hash);

		if (position.Occupied)
		{
			return {rox::core::AddressOf(m_Slots[position.Index].Item.Value), false};
		}

		rox::core::usize buckets = 0;
		Slot* slots = nullptr;

		const rox::core::usize addedOccupancy = m_BucketCount != 0 && controlBytes(m_Slots, m_BucketCount)[position.Index] == 1 ? 0 : 1;
		if (m_Size + m_Deleted + addedOccupancy > Capacity())
		{
			slots = tryAllocateSlots(m_Size + 1, buckets);

			if (slots == nullptr)
			{
				return {nullptr, false};
			}
		}

		Entry pending(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TArguments>(arguments)...);

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
	    requires requires { Emplace(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); }
	{
		return Emplace(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value));
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TKeyArgument, typename TValueArgument>
	typename HashMap<TKey, TValue, THash, TEqual>::InsertResult HashMap<TKey, TValue, THash, TEqual>::InsertRequired(TKeyArgument&& key, TValueArgument&& value) noexcept
	    requires requires { EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value)); }
	{
		return EmplaceRequired(rox::core::Forward<TKeyArgument>(key), rox::core::Forward<TValueArgument>(value));
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	TValue* HashMap<TKey, TValue, THash, TEqual>::Find(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		auto* entry = FindEntry(key);
		return entry == nullptr ? nullptr : rox::core::AddressOf(entry->Value);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	typename HashMap<TKey, TValue, THash, TEqual>::Entry* HashMap<TKey, TValue, THash, TEqual>::FindEntry(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key, normalizeHash(static_cast<rox::core::usize>(rox::core::AsConst(m_Hash)(key))));
		return index == m_BucketCount ? nullptr : &m_Slots[index].Item;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	const typename HashMap<TKey, TValue, THash, TEqual>::Entry* HashMap<TKey, TValue, THash, TEqual>::FindEntry(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key, normalizeHash(static_cast<rox::core::usize>(rox::core::AsConst(m_Hash)(key))));
		return index == m_BucketCount ? nullptr : &m_Slots[index].Item;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	const TValue* HashMap<TKey, TValue, THash, TEqual>::Find(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto* entry = FindEntry(key);
		return entry == nullptr ? nullptr : rox::core::AddressOf(entry->Value);
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
		const auto index = findIndex(key, normalizeHash(static_cast<rox::core::usize>(rox::core::AsConst(m_Hash)(key))));

		if (index == m_BucketCount)
		{
			return false;
		}

		rox::core::DestroyAt(&m_Slots[index].Item);
		controlBytes(m_Slots, m_BucketCount)[index] = 1;
		--m_Size;
		++m_Deleted;
		return true;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TPredicate>
	rox::core::usize HashMap<TKey, TValue, THash, TEqual>::EraseIf(TPredicate&& predicate) noexcept
	    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const Entry&>
	{
		rox::core::usize erased = 0;

		for (rox::core::usize index = 0; index < m_BucketCount; ++index)
		{
			Slot& slot = m_Slots[index];

			if (controlBytes(m_Slots, m_BucketCount)[index] <= 1 || !rox::core::Invoke(predicate, rox::core::AsConst(slot.Item)))
			{
				continue;
			}

			rox::core::DestroyAt(&slot.Item);
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
		const rox::core::usize bucketCount = m_BucketCount;
		rox::core::uint8* const controls = bucketCount != 0 ? controlBytes(slots, bucketCount) : nullptr;
		if constexpr (!rox::core::IsTriviallyDestructible<Entry>)
		{
			for (rox::core::usize index = 0; index < bucketCount; ++index)
			{
				if (controls[index] > 1)
				{
					rox::core::DestroyAt(&slots[index].Item);
					controls[index] = 0;
				}
			}
		}
		if (bucketCount != 0)
		{
			rox::core::SetBytes(controls, 0, bucketCount);
		}

		m_Size = 0;
		m_Deleted = 0;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::Reset() noexcept
	{
		if constexpr (!rox::core::IsTriviallyDestructible<Entry>)
		{
			Clear();
		}
		m_Size = 0;
		m_Deleted = 0;

		if (m_Slots != nullptr)
		{
			rox::core::DestroyN(m_Slots, m_BucketCount);
			m_Resource->Deallocate(m_Slots, {storageBytes(m_BucketCount), alignof(Slot)});
		}

		m_Slots = nullptr;
		m_BucketCount = 0;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	constexpr rox::core::usize HashMap<TKey, TValue, THash, TEqual>::storageBytes(const rox::core::usize buckets) noexcept
	{
		return buckets * (sizeof(Slot) + sizeof(rox::core::uint8));
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	rox::core::uint8* HashMap<TKey, TValue, THash, TEqual>::controlBytes(Slot* slots, const rox::core::usize buckets) noexcept
	{
		return reinterpret_cast<rox::core::uint8*>(slots + buckets);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	const rox::core::uint8* HashMap<TKey, TValue, THash, TEqual>::controlBytes(const Slot* slots, const rox::core::usize buckets) noexcept
	{
		return reinterpret_cast<const rox::core::uint8*>(slots + buckets);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	typename HashMap<TKey, TValue, THash, TEqual>::Slot* HashMap<TKey, TValue, THash, TEqual>::tryAllocateSlots(const rox::core::usize minimumCapacity,
	                                    rox::core::usize& buckets) noexcept
	{
		buckets = m_BucketCount == 0 ? 8 : m_BucketCount;
		constexpr rox::core::usize maximumBuckets = rox::core::NumericLimits<rox::core::usize>::max() / (sizeof(Slot) + sizeof(rox::core::uint8));

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
	constexpr rox::core::usize HashMap<TKey, TValue, THash, TEqual>::normalizeHash(const rox::core::usize hash) noexcept
	{
		return hash <= RESERVED_HASH_LIMIT ? hash + 2 : hash;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	typename HashMap<TKey, TValue, THash, TEqual>::InsertPosition HashMap<TKey, TValue, THash, TEqual>::findInsertPosition(const TLookup& key, const rox::core::usize hash) const noexcept
	{
		if (m_BucketCount == 0)
		{
			return {0, false};
		}
		const rox::core::uint8* const controls = controlBytes(m_Slots, m_BucketCount);
		rox::core::usize available = m_BucketCount;
		rox::core::usize index = hash & (m_BucketCount - 1);
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
	typename HashMap<TKey, TValue, THash, TEqual>::InsertResult HashMap<TKey, TValue, THash, TEqual>::insertPending(Entry& pending, const rox::core::usize hash, const rox::core::usize index) noexcept
	{
		Slot& slot = m_Slots[index];

		if (controlBytes(m_Slots, m_BucketCount)[index] == 1)
		{
			--m_Deleted;
		}

		rox::core::ConstructAt(&slot.Item, rox::core::Move(pending.m_Key), rox::core::Move(pending.Value));
		slot.Hash = hash;
		controlBytes(m_Slots, m_BucketCount)[index] = 0x80;
		++m_Size;
		return {rox::core::AddressOf(slot.Item.Value), true};
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	template <typename TLookup>
	rox::core::usize HashMap<TKey, TValue, THash, TEqual>::findIndex(const TLookup& key, const rox::core::usize hash) const noexcept
	{
		if (m_Size == 0)
		{
			return m_BucketCount;
		}

		const rox::core::usize bucketCount = m_BucketCount;
		const Slot* const slots = m_Slots;
		const rox::core::uint8* const controls = controlBytes(slots, bucketCount);
		rox::core::usize index = hash & (bucketCount - 1);

		for (;;)
		{
			const Slot& slot = slots[index];
			const rox::core::uint8 state = controls[index];
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
	rox::core::usize HashMap<TKey, TValue, THash, TEqual>::findAvailable(const rox::core::usize hash) const noexcept
	{
		const rox::core::uint8* const controls = controlBytes(m_Slots, m_BucketCount);
		rox::core::usize index = hash & (m_BucketCount - 1);

		while (controls[index] > 1)
		{
			index = (index + 1) & (m_BucketCount - 1);
		}

		return index;
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::rehash(const rox::core::usize bucketCount) noexcept
	{
		Slot* const slots = static_cast<Slot*>(m_Resource->AllocateRequired({storageBytes(bucketCount), alignof(Slot)}));
		relocate(slots, bucketCount);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::relocate(Slot* const slots, const rox::core::usize bucketCount) noexcept
	{
		Slot* oldSlots = m_Slots;
		const rox::core::usize oldCount = m_BucketCount;

		m_Slots = slots;
		m_BucketCount = bucketCount;
		rox::core::SetBytes(controlBytes(m_Slots, m_BucketCount), 0, m_BucketCount);

		for (rox::core::usize index = 0; index < bucketCount; ++index)
		{
			rox::core::ConstructAt(m_Slots + index);
		}

		for (rox::core::usize index = 0; index < oldCount; ++index)
		{
			Slot& previous = oldSlots[index];

			if (controlBytes(oldSlots, oldCount)[index] > 1)
			{
				const rox::core::usize destinationIndex = findAvailable(previous.Hash);
				Slot& destination = m_Slots[destinationIndex];
				rox::core::ConstructAt(&destination.Item, rox::core::Move(previous.Item.m_Key),
				                  rox::core::Move(previous.Item.Value));
				destination.Hash = previous.Hash;
				controlBytes(m_Slots, m_BucketCount)[destinationIndex] = 0x80;
				rox::core::DestroyAt(&previous.Item);
			}

			rox::core::DestroyAt(&previous);
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
		m_Slots = rox::core::Exchange(other.m_Slots, nullptr);
		m_BucketCount = rox::core::Exchange(other.m_BucketCount, 0);
		m_Size = rox::core::Exchange(other.m_Size, 0);
		m_Deleted = rox::core::Exchange(other.m_Deleted, 0);
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	void HashMap<TKey, TValue, THash, TEqual>::copySlotStorage(Slot* destination, const Slot* source, rox::core::usize count) noexcept
	{
		rox::core::CopyBytes(controlBytes(destination, count), controlBytes(source, count), count);
		// Bound streaming-copy sizes for tables that will be probed immediately.
		// Copy whole slots so each implicit-lifetime object fits within one copy.
		constexpr rox::core::usize BLOCK_SLOTS = sizeof(Slot) < 65536 ? 65536 / sizeof(Slot) : 1;
		while (count > BLOCK_SLOTS)
		{
			rox::core::CopyBytes(destination, source, BLOCK_SLOTS * sizeof(Slot));
			destination += BLOCK_SLOTS;
			source += BLOCK_SLOTS;
			count -= BLOCK_SLOTS;
		}
		rox::core::CopyBytes(destination, source, count * sizeof(Slot));
	}

	template <typename TKey, typename TValue, typename THash, typename TEqual>
	rox::core::usize HashMap<TKey, TValue, THash, TEqual>::copyBucketCount(const HashMap& other) noexcept
	{
		rox::core::usize compactBuckets = other.m_BucketCount;
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
		const rox::core::uint8* const sourceControls = controlBytes(other.m_Slots, other.m_BucketCount);
		rox::core::uint8* const destinationControls = controlBytes(m_Slots, m_BucketCount);
		for (rox::core::usize index = 0; index < other.m_BucketCount; ++index)
		{
			if (sourceControls[index] > 1)
			{
				const Slot& source = other.m_Slots[index];
				const rox::core::usize destinationIndex = findAvailable(source.Hash);
				Slot& destination = m_Slots[destinationIndex];
				rox::core::ConstructAt(&destination.Item, source.Item.m_Key, source.Item.Value);
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
			rox::core::SetBytes(controlBytes(m_Slots, m_BucketCount), 0, m_BucketCount);
			for (rox::core::usize index = 0; index < m_BucketCount; ++index)
			{
				rox::core::ConstructAt(m_Slots + index);
			}
			copyLiveEntries(other);
			m_Size = other.m_Size;
			return;
		}
		if constexpr (rox::core::IsTriviallyCopyable<Slot>)
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
			const rox::core::usize bucketCount = m_BucketCount;
			const rox::core::uint8* const sourceControls = controlBytes(sourceSlots, bucketCount);
			for (rox::core::usize index = 0; index < bucketCount; ++index)
			{
				Slot& destination = *rox::core::ConstructAt(destinationSlots + index);
				const Slot& source = sourceSlots[index];
				if (sourceControls[index] > 1)
				{
					rox::core::ConstructAt(&destination.Item, source.Item.m_Key, source.Item.Value);
					destination.Hash = source.Hash;
				}
			}
			rox::core::CopyBytes(controlBytes(destinationSlots, bucketCount), sourceControls, bucketCount);
		}
		m_Size = other.m_Size;
		m_Deleted = other.m_Deleted;
	}
}
