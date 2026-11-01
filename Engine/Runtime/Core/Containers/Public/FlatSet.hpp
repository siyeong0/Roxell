#pragma once

#include <Array.hpp>
#include <Types.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <Ordering.hpp>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// The resource and borrowed keys/comparator state outlive the set. Mutation requires exclusive access.
	template <typename TKey, typename TCompare = DefaultLess<TKey>> class FlatSet
	{
		static_assert(IsNothrowMoveConstructible<TKey> && IsNothrowDestructible<TKey>);
		static_assert(IsNothrowCopyConstructible<TCompare> && IsNothrowCopyAssignable<TCompare>);
		static_assert(IsNothrowDestructible<TCompare>);
		static_assert(IsNothrowInvocableR<bool, const TCompare&, const TKey&, const TKey&>);
		template <typename TLookup>
		static constexpr bool SUPPORTS_LOOKUP = IsNothrowInvocableR<bool, const TCompare&, const TKey&, const TLookup&> && IsNothrowInvocableR<bool, const TCompare&, const TLookup&, const TKey&>;

	public:
		struct InsertResult final
		{
			bool bSucceeded;
			bool bInserted;
		};

		explicit FlatSet(MemoryResource& resource, const TCompare& compare = {}) noexcept;

		FlatSet(const FlatSet& other) noexcept
		    requires IsNothrowCopyConstructible<TKey>;

		FlatSet(FlatSet&& other) noexcept;

		FlatSet& operator=(const FlatSet& other) noexcept
		    requires IsNothrowCopyConstructible<TKey>;

		FlatSet& operator=(FlatSet&& other) noexcept;

		~FlatSet() noexcept = default;

		[[nodiscard]] usize Size() const noexcept;
		[[nodiscard]] usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		// No-op reserve preserves views; growth invalidates every key view.
		[[nodiscard]] bool Reserve(usize capacity) noexcept;
		[[nodiscard]] bool Grow(usize capacity) noexcept;
		void ReserveRequired(usize capacity) noexcept;

		void Clear() noexcept;
		void Reset() noexcept;

		[[nodiscard]] Span<const TKey> View() const noexcept;

		[[nodiscard]] const TKey* begin() const noexcept;
		[[nodiscard]] const TKey* end() const noexcept;
		[[nodiscard]] const TKey& At(usize index) const noexcept;

		template <typename... TArguments>
		bool EmplaceRequired(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TKey, TArguments&&...>;

		template <typename TLookup = TKey>
		bool Erase(const TLookup& key) noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] const TKey* LowerBound(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TLookup = TKey>
		[[nodiscard]] const TKey* UpperBound(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TKeyArgument>
		bool InsertRequired(TKeyArgument&& key) noexcept
		    requires(IsSame<RemoveCvref<TKeyArgument>, TKey> &&
		             IsNothrowConstructible<TKey, TKeyArgument &&>);

		template <typename TLookup = TKey>
		[[nodiscard]] const TKey* Find(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

		template <typename TKeyArgument>
		[[nodiscard]] InsertResult Insert(TKeyArgument&& key) noexcept
		    requires requires { InsertRequired(core::Forward<TKeyArgument>(key)); };

		// Constructs the candidate before admission; constructor inputs may be consumed even on failure.
		template <typename... TArguments>
		[[nodiscard]] InsertResult Emplace(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TKey, TArguments&&...>;

		template <typename TLookup = TKey>
		[[nodiscard]] bool Contains(const TLookup& key) const noexcept
		    requires SUPPORTS_LOOKUP<TLookup>;

	private:
		template <typename TLookup> [[nodiscard]] usize lowerBoundIndex(const TLookup& key) const noexcept;
		template <typename TLookup> [[nodiscard]] usize upperBoundIndex(const TLookup& key) const noexcept;
		template <typename TLookup> [[nodiscard]] usize findIndex(const TLookup& key) const noexcept;

	private:
		Array<TKey> m_Values;
		TCompare m_Compare;
	};
}

namespace rox::core
{
	template <typename TKey, typename TCompare>
	FlatSet<TKey, TCompare>::FlatSet(MemoryResource& resource, const TCompare& compare) noexcept
	    : m_Values(resource)
	    , m_Compare(compare)
	{
	}

	template <typename TKey, typename TCompare>
	FlatSet<TKey, TCompare>::FlatSet(const FlatSet& other) noexcept
	    requires IsNothrowCopyConstructible<TKey>
	    : m_Values(other.m_Values)
	    , m_Compare(other.m_Compare)
	{
	}

	template <typename TKey, typename TCompare>
	FlatSet<TKey, TCompare>::FlatSet(FlatSet&& other) noexcept
	    : m_Values(core::Move(other.m_Values))
	    , m_Compare(other.m_Compare)
	{
	}

	template <typename TKey, typename TCompare>
	FlatSet<TKey, TCompare>& FlatSet<TKey, TCompare>::operator=(const FlatSet& other) noexcept
	    requires IsNothrowCopyConstructible<TKey>
	{
		if (this != &other)
		{
			m_Values = other.m_Values;
			m_Compare = other.m_Compare;
		}

		return *this;
	}

	template <typename TKey, typename TCompare>
	FlatSet<TKey, TCompare>& FlatSet<TKey, TCompare>::operator=(FlatSet&& other) noexcept
	{
		if (this != &other)
		{
			m_Values = core::Move(other.m_Values);
			m_Compare = other.m_Compare;
		}

		return *this;
	}

	template <typename TKey, typename TCompare>
	usize FlatSet<TKey, TCompare>::Size() const noexcept
	{
		return m_Values.Size();
	}

	template <typename TKey, typename TCompare>
	usize FlatSet<TKey, TCompare>::Capacity() const noexcept
	{
		return m_Values.Capacity();
	}

	template <typename TKey, typename TCompare>
	bool FlatSet<TKey, TCompare>::IsEmpty() const noexcept
	{
		return m_Values.IsEmpty();
	}

	template <typename TKey, typename TCompare>
	MemoryResource& FlatSet<TKey, TCompare>::GetResource() const noexcept
	{
		return m_Values.GetResource();
	}

	template <typename TKey, typename TCompare>
	bool FlatSet<TKey, TCompare>::Reserve(usize capacity) noexcept
	{
		return m_Values.Reserve(capacity);
	}

	template <typename TKey, typename TCompare>
	bool FlatSet<TKey, TCompare>::Grow(usize capacity) noexcept
	{
		return m_Values.Grow(capacity);
	}

	template <typename TKey, typename TCompare>
	void FlatSet<TKey, TCompare>::ReserveRequired(usize capacity) noexcept
	{
		m_Values.ReserveRequired(capacity);
	}

	template <typename TKey, typename TCompare>
	void FlatSet<TKey, TCompare>::Clear() noexcept
	{
		m_Values.Clear();
	}

	template <typename TKey, typename TCompare>
	void FlatSet<TKey, TCompare>::Reset() noexcept
	{
		m_Values.Reset();
	}

	template <typename TKey, typename TCompare>
	Span<const TKey> FlatSet<TKey, TCompare>::View() const noexcept
	{
		return m_Values.View();
	}

	template <typename TKey, typename TCompare>
	const TKey* FlatSet<TKey, TCompare>::begin() const noexcept
	{
		return m_Values.begin();
	}

	template <typename TKey, typename TCompare>
	const TKey* FlatSet<TKey, TCompare>::end() const noexcept
	{
		return m_Values.end();
	}

	template <typename TKey, typename TCompare>
	const TKey& FlatSet<TKey, TCompare>::At(usize index) const noexcept
	{
		ASSERT(index < Size(), "FlatSet index must identify a live key.");
		return m_Values[index];
	}

	template <typename TKey, typename TCompare>
	template <typename... TArguments>
	bool FlatSet<TKey, TCompare>::EmplaceRequired(TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TKey, TArguments&&...>
	{
		TKey pending(core::Forward<TArguments>(arguments)...);
		return InsertRequired(core::Move(pending));
	}

	template <typename TKey, typename TCompare>
	template <typename TLookup>
	bool FlatSet<TKey, TCompare>::Erase(const TLookup& key) noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key);

		if (index == Size())
		{
			return false;
		}

		m_Values.RemoveAt(index);
		return true;
	}

	template <typename TKey, typename TCompare>
	template <typename TLookup>
	const TKey* FlatSet<TKey, TCompare>::LowerBound(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = lowerBoundIndex(key);
		return index == Size() ? end() : core::AddressOf(m_Values[index]);
	}

	template <typename TKey, typename TCompare>
	template <typename TLookup>
	const TKey* FlatSet<TKey, TCompare>::UpperBound(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = upperBoundIndex(key);
		return index == Size() ? end() : core::AddressOf(m_Values[index]);
	}

	template <typename TKey, typename TCompare>
	template <typename TKeyArgument>
	bool FlatSet<TKey, TCompare>::InsertRequired(TKeyArgument&& key) noexcept
	    requires(IsSame<RemoveCvref<TKeyArgument>, TKey> && IsNothrowConstructible<TKey, TKeyArgument &&>)
	{
		const auto index = lowerBoundIndex(key);

		if (index != Size() && !core::AsConst(m_Compare)(core::AsConst(key), core::AsConst(m_Values[index])))
		{
			return false;
		}

		m_Values.InsertRequired(index, core::Forward<TKeyArgument>(key));
		return true;
	}

	template <typename TKey, typename TCompare>
	template <typename TLookup>
	const TKey* FlatSet<TKey, TCompare>::Find(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		const auto index = findIndex(key);
		return index == Size() ? nullptr : core::AddressOf(m_Values[index]);
	}

	template <typename TKey, typename TCompare>
	template <typename TKeyArgument>
	typename FlatSet<TKey, TCompare>::InsertResult FlatSet<TKey, TCompare>::Insert(TKeyArgument&& key) noexcept
	    requires requires { InsertRequired(core::Forward<TKeyArgument>(key)); }
	{
		const auto index = lowerBoundIndex(key);

		if (index != Size() && !core::AsConst(m_Compare)(core::AsConst(key), core::AsConst(m_Values[index])))
		{
			return {true, false};
		}

		const bool inserted = m_Values.Insert(index, core::Forward<TKeyArgument>(key)) != nullptr;
		return {inserted, inserted};
	}

	template <typename TKey, typename TCompare>
	template <typename... TArguments>
	typename FlatSet<TKey, TCompare>::InsertResult FlatSet<TKey, TCompare>::Emplace(TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TKey, TArguments&&...>
	{
		TKey key(core::Forward<TArguments>(arguments)...);
		return Insert(core::Move(key));
	}

	template <typename TKey, typename TCompare>
	template <typename TLookup>
	bool FlatSet<TKey, TCompare>::Contains(const TLookup& key) const noexcept
	    requires SUPPORTS_LOOKUP<TLookup>
	{
		return Find(key) != nullptr;
	}

	template <typename TKey, typename TCompare>
	template <typename TLookup>
	usize FlatSet<TKey, TCompare>::lowerBoundIndex(const TLookup& key) const noexcept
	{
		const TKey* const values = m_Values.Data();
		usize first = 0;
		usize count = Size();

		if (count == 0)
		{
			return 0;
		}

		while (count > 1)
		{
			const usize half = count / 2;
			const bool bBefore = m_Compare(values[first + half - 1], key);
			first = bBefore ? first + half : first;
			count -= half;
		}

		return first + static_cast<usize>(m_Compare(values[first], key));
	}

	template <typename TKey, typename TCompare>
	template <typename TLookup>
	usize FlatSet<TKey, TCompare>::upperBoundIndex(const TLookup& key) const noexcept
	{
		const TKey* const values = m_Values.Data();
		usize first = 0;
		usize count = Size();

		if (count == 0)
		{
			return 0;
		}

		while (count > 1)
		{
			const usize half = count / 2;
			const bool bAfter = m_Compare(key, values[first + half - 1]);
			first = bAfter ? first : first + half;
			count -= half;
		}

		return first + static_cast<usize>(!m_Compare(key, values[first]));
	}

	template <typename TKey, typename TCompare>
	template <typename TLookup>
	usize FlatSet<TKey, TCompare>::findIndex(const TLookup& key) const noexcept
	{
		const auto index = lowerBoundIndex(key);
		return index != Size() && !m_Compare(key, m_Values[index]) ? index : Size();
	}
}
