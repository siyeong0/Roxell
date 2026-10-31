#pragma once

#include <Assertion.h>
#include <Algorithms.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <CompilerAttributes.h>
#include <MemoryResource.h>
#include <Span.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// Mutation requires exclusive access. The resource must outlive all owned heap storage.
	template <typename TValue, rox::core::usize N> 
	class SmallVector final
	{
		static_assert(rox::core::IsObject<TValue> && !rox::core::IsArray<TValue>);
		static_assert(rox::core::IsSame<TValue, rox::core::RemoveCv<TValue>>);
		static_assert(rox::core::IsNothrowMoveConstructible<TValue> && rox::core::IsNothrowDestructible<TValue>);
		static_assert(N <= rox::core::NumericLimits<rox::core::usize>::max() / sizeof(TValue));

	public:
		[[nodiscard]] TValue& At(rox::core::usize index) noexcept;
		[[nodiscard]] TValue& Front() noexcept;
		[[nodiscard]] ROXELL_FORCE_INLINE TValue& Back() noexcept;
		[[nodiscard]] const TValue& At(rox::core::usize index) const noexcept;
		[[nodiscard]] const TValue& Front() const noexcept;
		[[nodiscard]] ROXELL_FORCE_INLINE const TValue& Back() const noexcept;

		explicit SmallVector(MemoryResource& resource) noexcept;

		~SmallVector() noexcept;

		SmallVector(const SmallVector& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		// Copy assignment retains its resource and invalidates destination views. Empty copies retain capacity.
		// Nontrivial copies are staged before releasing destination-owned objects, which may own the source.
		SmallVector& operator=(const SmallVector& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		// Except for self-assignment, the ownership graphs must be disjoint: clearing this vector must not
		// destroy the source or any object its copies require. Reuses capacity and invalidates destination views.
		SmallVector& AssignDisjoint(const SmallVector& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		SmallVector(SmallVector&& other) noexcept;

		SmallVector& operator=(SmallVector&& other) noexcept;

		[[nodiscard]] TValue* data() noexcept;
		[[nodiscard]] const TValue* data() const noexcept;
		[[nodiscard]] rox::core::usize size() const noexcept;
		[[nodiscard]] bool empty() const noexcept;

		[[nodiscard]] rox::core::usize Size() const noexcept;
		[[nodiscard]] rox::core::usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;
		[[nodiscard]] TValue* Data() noexcept;
		[[nodiscard]] const TValue* Data() const noexcept;

		[[nodiscard]] Span<TValue> View() noexcept;
		[[nodiscard]] Span<const TValue> View() const noexcept;

		// Predicates inspect const elements in order and stop at the first match; returned pointers are borrowed.
		template <typename TPredicate>
		[[nodiscard]] ROXELL_FORCE_INLINE TValue* FindIf(TPredicate&& predicate) noexcept
		    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		template <typename TPredicate>
		[[nodiscard]] ROXELL_FORCE_INLINE const TValue* FindIf(TPredicate&& predicate) const noexcept
		    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		[[nodiscard]] TValue* Find(const TValue& value) noexcept
		    requires requires(const TValue& left, const TValue& right) 
			{
			    { static_cast<bool>(left == right) } noexcept -> rox::core::SameAs<bool>;
		    };

		[[nodiscard]] const TValue* Find(const TValue& value) const noexcept
		    requires requires(const TValue& left, const TValue& right) 
			{
			    { static_cast<bool>(left == right) } noexcept -> rox::core::SameAs<bool>;
		    };

		[[nodiscard]] bool Contains(const TValue& value) const noexcept
		    requires requires(const TValue& left, const TValue& right) 
			{
			    { static_cast<bool>(left == right) } noexcept -> rox::core::SameAs<bool>;
		    };

		template <typename TPredicate>
		[[nodiscard]] bool ContainsIf(TPredicate&& predicate) const noexcept
		    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		TValue* begin() noexcept;
		const TValue* begin() const noexcept;
		TValue* end() noexcept;
		const TValue* end() const noexcept;

		ROXELL_FORCE_INLINE TValue& operator[](const rox::core::usize index) noexcept;
		ROXELL_FORCE_INLINE const TValue& operator[](const rox::core::usize index) const noexcept;

		[[nodiscard]] ROXELL_FORCE_INLINE bool Add(const TValue& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;
		[[nodiscard]] ROXELL_FORCE_INLINE bool Add(TValue&& value) noexcept;

		[[nodiscard]] bool Grow(const rox::core::usize capacity) noexcept;

		void ReserveRequired(const rox::core::usize capacity) noexcept;
		[[nodiscard]] bool Reserve(rox::core::usize capacity) noexcept;
		[[nodiscard]] bool Resize(rox::core::usize size) noexcept
		    requires rox::core::IsNothrowDefaultConstructible<TValue>;

		// Grows to size without initializing the new elements; the caller writes every one of them
		// before reading. Shrinking behaves like ResizeRequired.
		[[nodiscard]] ROXELL_FORCE_INLINE bool ResizeUninitialized(const rox::core::usize size) noexcept
		    requires rox::core::IsTriviallyCopyable<TValue>;

		// Growth consumes aliased arguments before relocating the original elements.
		template <typename... TArguments>
		ROXELL_FORCE_INLINE TValue& EmplaceRequired(TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<TValue, TArguments...>;

		ROXELL_FORCE_INLINE TValue& AddRequired(const TValue& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		ROXELL_FORCE_INLINE TValue& AddRequired(TValue&& value) noexcept;

		// Failure leaves the vector and aliased arguments unchanged.
		template <typename... TArguments>
		[[nodiscard]] ROXELL_FORCE_INLINE TValue* Emplace(TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<TValue, TArguments...>;

		[[nodiscard]] bool Append(const Span<const TValue> source) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		void AppendRequired(const Span<const TValue> source) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		void ResizeRequired(const rox::core::usize size) noexcept
		    requires rox::core::IsNothrowDefaultConstructible<TValue>;

		// Predicates inspect original elements in order through const references, without structural re-entry.
		// Captured element references must survive compaction. Views before the first removed element survive.
		template <typename TPredicate>
		rox::core::usize RemoveIf(TPredicate&& predicate) noexcept
		    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		// Invalidates the erased element, every element view at or after index, and the past-the-end iterator.
		void RemoveAt(const rox::core::usize index) noexcept;

		// Invalidates the erased and previous last element views, plus past-the-end; other element views remain valid.
		void RemoveAtSwap(const rox::core::usize index) noexcept;

		// Without growth, insertion invalidates views at and after index; an aliased rvalue is consumed first.
		TValue& InsertRequired(const rox::core::usize index, const TValue& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		TValue& InsertRequired(const rox::core::usize index, TValue&& value) noexcept;

		// Construct aliased arguments before moving existing elements or reallocating storage.
		template <typename... TArguments>
		TValue& EmplaceAtRequired(const rox::core::usize index, TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<TValue, TArguments&&...>;

		[[nodiscard]] TValue* Insert(const rox::core::usize index, const TValue& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		[[nodiscard]] TValue* Insert(const rox::core::usize index, TValue&& value) noexcept;

		template <typename... TArguments>
		[[nodiscard]] TValue* EmplaceAt(const rox::core::usize index, TArguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<TValue, TArguments...>;

		void PopBack() noexcept;

		void Clear() noexcept;

		void Reset() noexcept;

		// Success replaces output and invalidates all of its views. Failure leaves output unchanged.
		[[nodiscard]] bool CloneTo(MemoryResource& resource, SmallVector* const outSmallVector) const noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

	private:
		[[nodiscard]] ROXELL_NOINLINE bool tryGrowUninitialized(const rox::core::usize size) noexcept
		    requires rox::core::IsTriviallyCopyable<TValue>;

		template <typename... TArguments>
		TValue& insertValue(const rox::core::usize index, TArguments&&... arguments) noexcept;

		TValue* inlineData() noexcept;
		const TValue* inlineData() const noexcept;

		bool isInline() const noexcept;

		static constexpr rox::core::usize maximumCapacity() noexcept;

		void copyFrom(const SmallVector& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<TValue>;

		void moveFrom(SmallVector& other) noexcept;

		rox::core::usize growthCapacity(const rox::core::usize required) const noexcept;

		[[nodiscard]] TValue* tryAllocate(const rox::core::usize capacity) noexcept;

		TValue* allocate(const rox::core::usize capacity) noexcept;

		void destroyRange(const rox::core::usize first, const rox::core::usize last) noexcept;

		void moveTo(TValue* destination) noexcept;

		void releaseStorage() noexcept;

		void replaceStorage(TValue* data, const rox::core::usize capacity) noexcept;

		template <typename... TArguments> [[nodiscard]] TValue* tryEmplaceWithGrowth(TArguments&&... arguments) noexcept;

		template <typename... TArguments> TValue& addWithGrowth(TArguments&&... arguments) noexcept;

	private:
		alignas(TValue) rox::core::Byte m_Inline[N == 0 ? 1 : N * sizeof(TValue)];
		TValue* m_Data = inlineData();
		rox::core::usize m_Size = 0;
		rox::core::usize m_Capacity = N;
		MemoryResource* m_Resource;
	};
}

namespace rox::core
{
	template <typename TValue, rox::core::usize N>
	TValue& SmallVector<TValue, N>::At(rox::core::usize index) noexcept
	{
		return (*this)[index];
	}

	template <typename TValue, rox::core::usize N>
	TValue& SmallVector<TValue, N>::Front() noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::Front requires a non-empty range.");
		return (*this)[0];
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::Back() noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::Back requires a non-empty range.");
		return (*this)[m_Size - 1];
	}

	template <typename TValue, rox::core::usize N>
	const TValue& SmallVector<TValue, N>::At(rox::core::usize index) const noexcept
	{
		return (*this)[index];
	}

	template <typename TValue, rox::core::usize N>
	const TValue& SmallVector<TValue, N>::Front() const noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::Front requires a non-empty range.");
		return (*this)[0];
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_FORCE_INLINE const TValue& SmallVector<TValue, N>::Back() const noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::Back requires a non-empty range.");
		return (*this)[m_Size - 1];
	}

	template <typename TValue, rox::core::usize N>
	SmallVector<TValue, N>::SmallVector(MemoryResource& resource) noexcept
	    : m_Resource(&resource)
	{
	}

	template <typename TValue, rox::core::usize N>
	SmallVector<TValue, N>::~SmallVector() noexcept
	{
		destroyRange(0, m_Size);
		releaseStorage();
	}

	template <typename TValue, rox::core::usize N>
	SmallVector<TValue, N>::SmallVector(const SmallVector& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	    : SmallVector(*other.m_Resource)
	{
		copyFrom(other);
	}

	template <typename TValue, rox::core::usize N>
	SmallVector<TValue, N>& SmallVector<TValue, N>::operator=(const SmallVector& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		if (this == &other)
		{
			return *this;
		}
		if (other.m_Size == 0)
		{
			Clear();
			return *this;
		}
		if constexpr (rox::core::IsTriviallyCopyable<TValue>)
		{
			if (other.m_Size <= m_Capacity)
			{
				rox::core::CopyBytes(m_Data, other.Data(), other.m_Size * sizeof(TValue));
				m_Size = other.m_Size;
				return *this;
			}
		}
		SmallVector replacement(*m_Resource);
		replacement.copyFrom(other);
		Reset();
		moveFrom(replacement);
		return *this;
	}

	template <typename TValue, rox::core::usize N>
	SmallVector<TValue, N>& SmallVector<TValue, N>::AssignDisjoint(const SmallVector& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		if (this == &other)
		{
			return *this;
		}
		if constexpr (rox::core::IsTriviallyCopyable<TValue>)
		{
			return *this = other;
		}
		else
		{
			if (other.m_Size > m_Capacity)
			{
				return *this = other;
			}
			Clear();
			for (const TValue& value : other)
			{
				rox::core::ConstructAt(m_Data + m_Size, value);
				++m_Size;
			}
			return *this;
		}
	}

	template <typename TValue, rox::core::usize N>
	SmallVector<TValue, N>::SmallVector(SmallVector&& other) noexcept
	    : SmallVector(*other.m_Resource)
	{
		moveFrom(other);
	}

	template <typename TValue, rox::core::usize N>
	SmallVector<TValue, N>& SmallVector<TValue, N>::operator=(SmallVector&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		if (m_Resource == other.m_Resource)
		{
			Reset();
			moveFrom(other);
		}
		else
		{
			SmallVector replacement(*m_Resource);
			replacement.ReserveRequired(other.m_Size);

			for (TValue& value : other)
			{
				replacement.EmplaceRequired(rox::core::Move(value));
			}
			Reset();
			other.Reset();
			moveFrom(replacement);
		}

		return *this;
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::data() noexcept
	{
		return Data();
	}

	template <typename TValue, rox::core::usize N>
	const TValue* SmallVector<TValue, N>::data() const noexcept
	{
		return Data();
	}

	template <typename TValue, rox::core::usize N>
	rox::core::usize SmallVector<TValue, N>::size() const noexcept
	{
		return Size();
	}

	template <typename TValue, rox::core::usize N>
	bool SmallVector<TValue, N>::empty() const noexcept
	{
		return IsEmpty();
	}

	template <typename TValue, rox::core::usize N>
	rox::core::usize SmallVector<TValue, N>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename TValue, rox::core::usize N>
	rox::core::usize SmallVector<TValue, N>::Capacity() const noexcept
	{
		return m_Capacity;
	}

	template <typename TValue, rox::core::usize N>
	bool SmallVector<TValue, N>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename TValue, rox::core::usize N>
	MemoryResource& SmallVector<TValue, N>::GetResource() const noexcept
	{
		return *m_Resource;
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::Data() noexcept
	{
		return m_Data;
	}

	template <typename TValue, rox::core::usize N>
	const TValue* SmallVector<TValue, N>::Data() const noexcept
	{
		return m_Data;
	}

	template <typename TValue, rox::core::usize N>
	Span<TValue> SmallVector<TValue, N>::View() noexcept
	{
		return {m_Data, m_Size};
	}

	template <typename TValue, rox::core::usize N>
	Span<const TValue> SmallVector<TValue, N>::View() const noexcept
	{
		return {m_Data, m_Size};
	}

	template <typename TValue, rox::core::usize N>
	template <typename TPredicate>
	ROXELL_FORCE_INLINE TValue* SmallVector<TValue, N>::FindIf(TPredicate&& predicate) noexcept
	    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		if (m_Size == 0)
		{
			return nullptr;
		}
		TValue* cursor = Data();
		constexpr rox::core::usize BLOCK_SIZE = sizeof(TValue) <= 16 ? 8 : 4;
		rox::core::usize remaining = m_Size;
		TValue* const blockEnd = cursor + (m_Size - m_Size % BLOCK_SIZE);
		TValue* const end = cursor + m_Size;
		while (BLOCK_SIZE == 8 ? cursor != blockEnd : remaining >= 4)
		{
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if constexpr (BLOCK_SIZE == 8)
			{
				if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
			}
			else
			{
				remaining -= 4;
			}
		}
		while (BLOCK_SIZE == 8 ? cursor != end : remaining != 0)
		{
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if constexpr (BLOCK_SIZE == 4)
			{
				--remaining;
			}
		}
		return nullptr;
	}

	template <typename TValue, rox::core::usize N>
	template <typename TPredicate>
	ROXELL_FORCE_INLINE const TValue* SmallVector<TValue, N>::FindIf(TPredicate&& predicate) const noexcept
	    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		if (m_Size == 0)
		{
			return nullptr;
		}
		const TValue* cursor = Data();
		constexpr rox::core::usize BLOCK_SIZE = sizeof(TValue) <= 16 ? 8 : 4;
		rox::core::usize remaining = m_Size;
		const TValue* const blockEnd = cursor + (m_Size - m_Size % BLOCK_SIZE);
		const TValue* const end = cursor + m_Size;
		while (BLOCK_SIZE == 8 ? cursor != blockEnd : remaining >= 4)
		{
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if constexpr (BLOCK_SIZE == 8)
			{
				if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
			}
			else
			{
				remaining -= 4;
			}
		}
		while (BLOCK_SIZE == 8 ? cursor != end : remaining != 0)
		{
			if (rox::core::Invoke(predicate, rox::core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if constexpr (BLOCK_SIZE == 4)
			{
				--remaining;
			}
		}
		return nullptr;
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::Find(const TValue& value) noexcept
	    requires requires(const TValue& left, const TValue& right) {
		    { static_cast<bool>(left == right) } noexcept -> rox::core::SameAs<bool>;
	    }
	{
		if constexpr (rox::core::IsIntegral<TValue>)
		{
			if (m_Size == 0)
			{
				return nullptr;
			}
			auto* const first = Data();
			auto* const last = first + m_Size;
			auto* const found = rox::core::Find(first, last, value);
			return found == last ? nullptr : found;
		}
		else
		{
			return FindIf(
			    [&value](const TValue& candidate) noexcept
			    {
				    return static_cast<bool>(candidate == value);
			    });
		}
	}

	template <typename TValue, rox::core::usize N>
	const TValue* SmallVector<TValue, N>::Find(const TValue& value) const noexcept
	    requires requires(const TValue& left, const TValue& right) {
		    { static_cast<bool>(left == right) } noexcept -> rox::core::SameAs<bool>;
	    }
	{
		if constexpr (rox::core::IsIntegral<TValue>)
		{
			if (m_Size == 0)
			{
				return nullptr;
			}
			auto* const first = Data();
			auto* const last = first + m_Size;
			auto* const found = rox::core::Find(first, last, value);
			return found == last ? nullptr : found;
		}
		else
		{
			return FindIf(
			    [&value](const TValue& candidate) noexcept
			    {
				    return static_cast<bool>(candidate == value);
			    });
		}
	}

	template <typename TValue, rox::core::usize N>
	bool SmallVector<TValue, N>::Contains(const TValue& value) const noexcept
	    requires requires(const TValue& left, const TValue& right) {
		    { static_cast<bool>(left == right) } noexcept -> rox::core::SameAs<bool>;
	    }
	{
		return Find(value) != nullptr;
	}

	template <typename TValue, rox::core::usize N>
	template <typename TPredicate>
	bool SmallVector<TValue, N>::ContainsIf(TPredicate&& predicate) const noexcept
	    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		return FindIf(rox::core::Forward<TPredicate>(predicate)) != nullptr;
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::begin() noexcept
	{
		return m_Data;
	}

	template <typename TValue, rox::core::usize N>
	const TValue* SmallVector<TValue, N>::begin() const noexcept
	{
		return m_Data;
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::end() noexcept
	{
		return m_Size == 0 ? m_Data : m_Data + m_Size;
	}

	template <typename TValue, rox::core::usize N>
	const TValue* SmallVector<TValue, N>::end() const noexcept
	{
		return m_Size == 0 ? m_Data : m_Data + m_Size;
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::operator[](const rox::core::usize index) noexcept
	{
		ASSERT(index < m_Size, "SmallVector index must address a live element.");
		return m_Data[index];
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_FORCE_INLINE const TValue& SmallVector<TValue, N>::operator[](const rox::core::usize index) const noexcept
	{
		ASSERT(index < m_Size, "SmallVector index must address a live element.");
		return m_Data[index];
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_FORCE_INLINE bool SmallVector<TValue, N>::Add(const TValue& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		return Emplace(value) != nullptr;
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_FORCE_INLINE bool SmallVector<TValue, N>::Add(TValue&& value) noexcept
	{
		return Emplace(rox::core::Move(value)) != nullptr;
	}

	template <typename TValue, rox::core::usize N>
	bool SmallVector<TValue, N>::Grow(const rox::core::usize capacity) noexcept
	{
		if (capacity <= m_Capacity)
		{
			return true;
		}
		return capacity <= maximumCapacity() && Reserve(growthCapacity(capacity));
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::ReserveRequired(const rox::core::usize capacity) noexcept
	{
		ASSERT(capacity <= maximumCapacity(), "SmallVector::ReserveRequired capacity must fit its representable limit.");

		if (capacity <= m_Capacity)
		{
			return;
		}
		TValue* const data = allocate(capacity);
		moveTo(data);
		replaceStorage(data, capacity);
	}

	template <typename TValue, rox::core::usize N>
	bool SmallVector<TValue, N>::Reserve(rox::core::usize capacity) noexcept
	{
		if (capacity <= m_Capacity)
		{
			return true;
		}

		if (capacity > maximumCapacity())
		{
			return false;
		}

		auto* data = static_cast<TValue*>(m_Resource->Allocate({capacity * sizeof(TValue), alignof(TValue)}));
		if (data == nullptr)
		{
			return false;
		}
		moveTo(data);
		replaceStorage(data, capacity);
		return true;
	}

	template <typename TValue, rox::core::usize N>
	bool SmallVector<TValue, N>::Resize(rox::core::usize size) noexcept
	    requires rox::core::IsNothrowDefaultConstructible<TValue>
	{
		if (!Reserve(size))
		{
			return false;
		}
		ResizeRequired(size);
		return true;
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_FORCE_INLINE bool SmallVector<TValue, N>::ResizeUninitialized(const rox::core::usize size) noexcept
	    requires rox::core::IsTriviallyCopyable<TValue>
	{
		// Trivially copyable values have trivial destruction, including when shrinking.
		if (size <= m_Capacity)
		{
			m_Size = size;
			return true;
		}
		return tryGrowUninitialized(size);
	}

	template <typename TValue, rox::core::usize N>
	template <typename... TArguments>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::EmplaceRequired(TArguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<TValue, TArguments...>
	{
		if (m_Size < m_Capacity)
		{
			TValue* const value = rox::core::ConstructAt(m_Data + m_Size, rox::core::Forward<TArguments>(arguments)...);
			++m_Size;
			return *value;
		}
		ASSERT(m_Size < maximumCapacity(), "SmallVector::EmplaceRequired requires representable growth.");
		return addWithGrowth(rox::core::Forward<TArguments>(arguments)...);
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::AddRequired(const TValue& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		return EmplaceRequired(value);
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::AddRequired(TValue&& value) noexcept
	{
		return EmplaceRequired(rox::core::Move(value));
	}

	template <typename TValue, rox::core::usize N>
	template <typename... TArguments>
	ROXELL_FORCE_INLINE TValue* SmallVector<TValue, N>::Emplace(TArguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<TValue, TArguments...>
	{
		if (m_Size < m_Capacity)
		{
			TValue* const value = rox::core::ConstructAt(m_Data + m_Size, rox::core::Forward<TArguments>(arguments)...);
			++m_Size;
			return value;
		}
		return tryEmplaceWithGrowth(rox::core::Forward<TArguments>(arguments)...);
	}

	template <typename TValue, rox::core::usize N>
	bool SmallVector<TValue, N>::Append(const Span<const TValue> source) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		if (source.Size() > maximumCapacity() - m_Size)
		{
			return false;
		}
		if (source.Size() > m_Capacity - m_Size)
		{
			const rox::core::usize capacity = growthCapacity(m_Size + source.Size());
			TValue* const data = tryAllocate(capacity);
			if (data == nullptr)
			{
				return false;
			}
			rox::core::UninitializedCopyN(source.Data(), source.Size(), data + m_Size);
			moveTo(data);
			replaceStorage(data, capacity);
			m_Size += source.Size();
			return true;
		}
		AppendRequired(source);
		return true;
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::AppendRequired(const Span<const TValue> source) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		ASSERT(source.Size() <= maximumCapacity() - m_Size,
		       "SmallVector::AppendRequired size must fit its representable limit.");

		if (source.IsEmpty())
		{
			return;
		}

		const rox::core::usize required = m_Size + source.Size();
		const bool bGrow = required > m_Capacity;
		const rox::core::usize capacity = bGrow ? growthCapacity(required) : m_Capacity;
		TValue* const data = bGrow ? allocate(capacity) : m_Data;
		if constexpr (rox::core::IsTriviallyCopyable<TValue>)
		{
			rox::core::CopyBytes(data + m_Size, source.Data(), source.Size() * sizeof(TValue));
		}
		else
		{
			for (rox::core::usize index = 0; index < source.Size(); ++index)
			{
				rox::core::ConstructAt(data + m_Size + index, source[index]);
			}
		}
		if (bGrow)
		{
			moveTo(data);
			replaceStorage(data, capacity);
		}

		m_Size = required;
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::ResizeRequired(const rox::core::usize size) noexcept
	    requires rox::core::IsNothrowDefaultConstructible<TValue>
	{
		ASSERT(size <= maximumCapacity(), "SmallVector::ResizeRequired size must fit its representable limit.");

		if (size < m_Size)
		{
			destroyRange(size, m_Size);
			m_Size = size;
			return;
		}
		ReserveRequired(size);
		if constexpr (rox::core::IsTrivial<TValue>)
		{
			if (size != m_Size)
			{
				rox::core::ValueConstructN(m_Data + m_Size, size - m_Size);
			}
			m_Size = size;
		}
		else
		{
			while (m_Size < size)
			{
				rox::core::ConstructAt(m_Data + m_Size);
				++m_Size;
			}
		}
	}

	template <typename TValue, rox::core::usize N>
	template <typename TPredicate>
	rox::core::usize SmallVector<TValue, N>::RemoveIf(TPredicate&& predicate) noexcept
	    requires rox::core::IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		TValue* const data = m_Data;
		const rox::core::usize oldSize = m_Size;
		rox::core::usize destination = 0;
		while (destination < oldSize && !rox::core::Invoke(predicate, rox::core::AsConst(data[destination])))
		{
			++destination;
		}
		if (destination == oldSize)
		{
			return 0;
		}
		rox::core::DestroyAt(data + destination);
		// The first erased value leaves a gap, so every later survivor moves into a distinct slot.
		for (rox::core::usize sourceIndex = destination + 1; sourceIndex < oldSize; ++sourceIndex)
		{
			if (rox::core::Invoke(predicate, rox::core::AsConst(data[sourceIndex])))
			{
				rox::core::DestroyAt(data + sourceIndex);
			}
			else
			{
				rox::core::detail::RelocateAtUnchecked(data + destination, data + sourceIndex);
				++destination;
			}
		}

		const rox::core::usize removed = oldSize - destination;
		m_Size = destination;
		return removed;
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::RemoveAt(const rox::core::usize index) noexcept
	{
		ASSERT(index < m_Size, "SmallVector::RemoveAt index must identify an initialized element.");
		rox::core::DestroyAt(m_Data + index);

		if constexpr (rox::core::IsTriviallyCopyable<TValue>)
		{
			if (index + 1 < m_Size)
			{
				rox::core::MoveBytes(m_Data + index, m_Data + index + 1,
				                     (m_Size - index - 1) * sizeof(TValue));
				rox::core::DestroyAt(m_Data + m_Size - 1);
			}
		}
		else
		{
			TValue* const data = m_Data;
			TValue* const last = data + m_Size;
			for (TValue* source = data + index + 1; source != last; ++source)
			{
				rox::core::detail::RelocateAtUnchecked(source - 1, source);
			}
		}
		--m_Size;
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::RemoveAtSwap(const rox::core::usize index) noexcept
	{
		ASSERT(index < m_Size, "SmallVector::RemoveAtSwap index must identify an initialized element.");
		const rox::core::usize lastIndex = m_Size - 1;
		rox::core::DestroyAt(m_Data + index);

		if (index != lastIndex)
		{
			rox::core::detail::RelocateAtUnchecked(m_Data + index, m_Data + lastIndex);
		}
		--m_Size;
	}

	template <typename TValue, rox::core::usize N>
	TValue& SmallVector<TValue, N>::InsertRequired(const rox::core::usize index, const TValue& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		return insertValue(index, value);
	}

	template <typename TValue, rox::core::usize N>
	TValue& SmallVector<TValue, N>::InsertRequired(const rox::core::usize index, TValue&& value) noexcept
	{
		return insertValue(index, rox::core::Move(value));
	}

	template <typename TValue, rox::core::usize N>
	template <typename... TArguments>
	TValue& SmallVector<TValue, N>::EmplaceAtRequired(const rox::core::usize index, TArguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<TValue, TArguments&&...>
	{
		return insertValue(index, rox::core::Forward<TArguments>(arguments)...);
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::Insert(const rox::core::usize index, const TValue& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		return EmplaceAt(index, value);
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::Insert(const rox::core::usize index, TValue&& value) noexcept
	{
		return EmplaceAt(index, rox::core::Move(value));
	}

	template <typename TValue, rox::core::usize N>
	template <typename... TArguments>
	TValue* SmallVector<TValue, N>::EmplaceAt(const rox::core::usize index, TArguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<TValue, TArguments...>
	{
		ASSERT(index <= m_Size, "SmallVector::EmplaceAt index must fit the initialized range including its end.");
		if (m_Size == maximumCapacity())
		{
			return nullptr;
		}
		if (m_Size < m_Capacity)
		{
			return rox::core::AddressOf(insertValue(index, rox::core::Forward<TArguments>(arguments)...));
		}
		const rox::core::usize capacity = growthCapacity(m_Size + 1);
		TValue* const data = tryAllocate(capacity);
		if (data == nullptr)
		{
			return nullptr;
		}
		rox::core::ConstructAt(data + index, rox::core::Forward<TArguments>(arguments)...);
		for (rox::core::usize source = 0; source < m_Size; ++source)
		{
			if constexpr (rox::core::Relocation<TValue>::IsCustomized)
			{
				rox::core::detail::RelocateAtUnchecked(data + source + (source >= index ? 1 : 0), m_Data + source);
			}
			else
			{
				rox::core::ConstructAt(data + source + (source >= index ? 1 : 0), rox::core::Move(m_Data[source]));
			}
		}
		replaceStorage(data, capacity);
		++m_Size;
		return m_Data + index;
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::PopBack() noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::PopBack requires a live element.");
		rox::core::DestroyAt(m_Data + m_Size - 1);
		--m_Size;
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::Clear() noexcept
	{
		destroyRange(0, m_Size);
		m_Size = 0;
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::Reset() noexcept
	{
		Clear();
		releaseStorage();
		m_Data = inlineData();
		m_Capacity = N;
	}

	template <typename TValue, rox::core::usize N>
	bool SmallVector<TValue, N>::CloneTo(MemoryResource& resource, SmallVector* const outSmallVector) const noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		ASSERT(outSmallVector != nullptr, "SmallVector::CloneTo requires a valid output pointer.");
		ASSERT(&outSmallVector->GetResource() == &resource,
		       "SmallVector::CloneTo output must use the requested memory resource.");
		SmallVector clone(resource);

		if (!clone.Reserve(m_Size))
		{
			return false;
		}
		clone.AppendRequired(View());
		*outSmallVector = rox::core::Move(clone);
		return true;
	}

	template <typename TValue, rox::core::usize N>
	ROXELL_NOINLINE bool SmallVector<TValue, N>::tryGrowUninitialized(const rox::core::usize size) noexcept
	    requires rox::core::IsTriviallyCopyable<TValue>
	{
		if (!Reserve(size))
		{
			return false;
		}
		m_Size = size;
		return true;
	}

	template <typename TValue, rox::core::usize N>
	template <typename... TArguments>
	TValue& SmallVector<TValue, N>::insertValue(const rox::core::usize index, TArguments&&... arguments) noexcept
	{
		ASSERT(index <= m_Size, "SmallVector::InsertRequired index must fit the initialized range including its end.");
		ASSERT(m_Size != maximumCapacity(), "SmallVector::InsertRequired cannot exceed its maximum representable capacity.");
		TValue pending(rox::core::Forward<TArguments>(arguments)...);

		if (m_Size == m_Capacity)
		{
			ReserveRequired(growthCapacity(m_Size + 1));
		}

		if constexpr (rox::core::IsTriviallyCopyable<TValue>)
		{
			if (index != m_Size)
			{
				rox::core::MoveBytes(m_Data + index + 1, m_Data + index,
				                     (m_Size - index) * sizeof(TValue));
			}
		}
		else
		{
			TValue* const data = m_Data;
			TValue* const first = data + index;
			for (TValue* destination = data + m_Size; destination != first; --destination)
			{
				rox::core::detail::RelocateAtUnchecked(destination, destination - 1);
			}
		}

		rox::core::ConstructAt(m_Data + index, rox::core::Move(pending));
		++m_Size;
		return m_Data[index];
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::inlineData() noexcept
	{
		return N == 0 ? nullptr : reinterpret_cast<TValue*>(m_Inline);
	}

	template <typename TValue, rox::core::usize N>
	const TValue* SmallVector<TValue, N>::inlineData() const noexcept
	{
		return N == 0 ? nullptr : reinterpret_cast<const TValue*>(m_Inline);
	}

	template <typename TValue, rox::core::usize N>
	bool SmallVector<TValue, N>::isInline() const noexcept
	{
		return m_Data == inlineData();
	}

	template <typename TValue, rox::core::usize N>
	constexpr rox::core::usize SmallVector<TValue, N>::maximumCapacity() noexcept
	{
		return rox::core::NumericLimits<rox::core::usize>::max() / sizeof(TValue);
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::copyFrom(const SmallVector& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<TValue>
	{
		ReserveRequired(other.m_Size);

		if constexpr (rox::core::IsTriviallyCopyable<TValue>)
		{
			if (other.m_Size != 0)
			{
				rox::core::CopyBytes(m_Data, other.Data(), other.m_Size * sizeof(TValue));
			}
			m_Size = other.m_Size;
		}
		else
		{
			for (const TValue& value : other)
			{
				EmplaceRequired(value);
			}
		}
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::moveFrom(SmallVector& other) noexcept
	{
		if (other.isInline())
		{
			other.moveTo(m_Data);
			m_Size = other.m_Size;
			if constexpr (rox::core::Relocation<TValue>::IsCustomized)
			{
				other.m_Size = 0;
			}
			else
			{
				other.Clear();
			}
		}
		else
		{
			m_Data = rox::core::Exchange(other.m_Data, other.inlineData());
			m_Size = rox::core::Exchange(other.m_Size, 0);
			m_Capacity = rox::core::Exchange(other.m_Capacity, N);
		}
	}

	template <typename TValue, rox::core::usize N>
	rox::core::usize SmallVector<TValue, N>::growthCapacity(const rox::core::usize required) const noexcept
	{
		const rox::core::usize doubled = m_Capacity > maximumCapacity() / 2 ? maximumCapacity() : m_Capacity * 2;
		return rox::core::Max(required, rox::core::Max(rox::core::usize{1}, doubled));
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::tryAllocate(const rox::core::usize capacity) noexcept
	{
		if (capacity == 0 || capacity > maximumCapacity())
		{
			return nullptr;
		}
		return static_cast<TValue*>(m_Resource->Allocate({capacity * sizeof(TValue), alignof(TValue)}));
	}

	template <typename TValue, rox::core::usize N>
	TValue* SmallVector<TValue, N>::allocate(const rox::core::usize capacity) noexcept
	{
		return static_cast<TValue*>(m_Resource->AllocateRequired({capacity * sizeof(TValue), alignof(TValue)}));
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::destroyRange(const rox::core::usize first, const rox::core::usize last) noexcept
	{
		if constexpr (!rox::core::IsTriviallyDestructible<TValue>)
		{
			for (rox::core::usize index = first; index < last; ++index)
			{
				rox::core::DestroyAt(m_Data + index);
			}
		}
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::moveTo(TValue* destination) noexcept
	{
		if constexpr (rox::core::IsTriviallyCopyable<TValue>)
		{
			if (m_Size != 0)
			{
				rox::core::CopyBytes(destination, m_Data, m_Size * sizeof(TValue));
			}
		}
		else
		{
			for (rox::core::usize index = 0; index < m_Size; ++index)
			{
				if constexpr (rox::core::Relocation<TValue>::IsCustomized)
				{
					rox::core::detail::RelocateAtUnchecked(destination + index, m_Data + index);
				}
				else
				{
					rox::core::ConstructAt(destination + index, rox::core::Move(m_Data[index]));
				}
			}
		}
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::releaseStorage() noexcept
	{
		if (!isInline())
		{
			m_Resource->Deallocate(m_Data, {m_Capacity * sizeof(TValue), alignof(TValue)});
		}
	}

	template <typename TValue, rox::core::usize N>
	void SmallVector<TValue, N>::replaceStorage(TValue* data, const rox::core::usize capacity) noexcept
	{
		if constexpr (!rox::core::Relocation<TValue>::IsCustomized)
		{
			destroyRange(0, m_Size);
		}
		releaseStorage();
		m_Data = data;
		m_Capacity = capacity;
	}

	template <typename TValue, rox::core::usize N>
	template <typename... TArguments>
	TValue* SmallVector<TValue, N>::tryEmplaceWithGrowth(TArguments&&... arguments) noexcept
	{
		if (m_Size == maximumCapacity())
		{
			return nullptr;
		}
		const rox::core::usize capacity = growthCapacity(m_Size + 1);
		TValue* const data = tryAllocate(capacity);
		if (data == nullptr)
		{
			return nullptr;
		}
		TValue* const value = rox::core::ConstructAt(data + m_Size, rox::core::Forward<TArguments>(arguments)...);
		moveTo(data);
		replaceStorage(data, capacity);
		++m_Size;
		return value;
	}

	template <typename TValue, rox::core::usize N>
	template <typename... TArguments>
	TValue& SmallVector<TValue, N>::addWithGrowth(TArguments&&... arguments) noexcept
	{
		const rox::core::usize capacity = growthCapacity(m_Size + 1);
		TValue* const data = allocate(capacity);
		TValue* const value = rox::core::ConstructAt(data + m_Size, rox::core::Forward<TArguments>(arguments)...);
		moveTo(data);
		replaceStorage(data, capacity);
		++m_Size;
		return *value;
	}
}
