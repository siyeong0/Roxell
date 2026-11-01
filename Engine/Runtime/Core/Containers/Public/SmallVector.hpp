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
	template <typename TValue, usize N>
	class SmallVector final
	{
		static_assert(IsObject<TValue> && !IsArray<TValue>);
		static_assert(IsSame<TValue, RemoveCv<TValue>>);
		static_assert(IsNothrowMoveConstructible<TValue> && IsNothrowDestructible<TValue>);
		static_assert(N <= NumericLimits<usize>::max() / sizeof(TValue));

	public:
		[[nodiscard]] TValue& At(usize index) noexcept;
		[[nodiscard]] TValue& Front() noexcept;
		[[nodiscard]] ROXELL_FORCE_INLINE TValue& Back() noexcept;
		[[nodiscard]] const TValue& At(usize index) const noexcept;
		[[nodiscard]] const TValue& Front() const noexcept;
		[[nodiscard]] ROXELL_FORCE_INLINE const TValue& Back() const noexcept;

		explicit SmallVector(MemoryResource& resource) noexcept;

		~SmallVector() noexcept;

		SmallVector(const SmallVector& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		// Copy assignment retains its resource and invalidates destination views. Empty copies retain capacity.
		// Nontrivial copies are staged before releasing destination-owned objects, which may own the source.
		SmallVector& operator=(const SmallVector& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		// Except for self-assignment, the ownership graphs must be disjoint: clearing this vector must not
		// destroy the source or any object its copies require. Reuses capacity and invalidates destination views.
		SmallVector& AssignDisjoint(const SmallVector& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		SmallVector(SmallVector&& other) noexcept;

		SmallVector& operator=(SmallVector&& other) noexcept;

		[[nodiscard]] TValue* data() noexcept;
		[[nodiscard]] const TValue* data() const noexcept;
		[[nodiscard]] usize size() const noexcept;
		[[nodiscard]] bool empty() const noexcept;

		[[nodiscard]] usize Size() const noexcept;
		[[nodiscard]] usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;
		[[nodiscard]] TValue* Data() noexcept;
		[[nodiscard]] const TValue* Data() const noexcept;

		[[nodiscard]] Span<TValue> View() noexcept;
		[[nodiscard]] Span<const TValue> View() const noexcept;

		// Predicates inspect const elements in order and stop at the first match; returned pointers are borrowed.
		template <typename TPredicate>
		[[nodiscard]] ROXELL_FORCE_INLINE TValue* FindIf(TPredicate&& predicate) noexcept
		    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		template <typename TPredicate>
		[[nodiscard]] ROXELL_FORCE_INLINE const TValue* FindIf(TPredicate&& predicate) const noexcept
		    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		[[nodiscard]] TValue* Find(const TValue& value) noexcept
		    requires requires(const TValue& left, const TValue& right) 
			{
			    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
		    };

		[[nodiscard]] const TValue* Find(const TValue& value) const noexcept
		    requires requires(const TValue& left, const TValue& right) 
			{
			    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
		    };

		[[nodiscard]] bool Contains(const TValue& value) const noexcept
		    requires requires(const TValue& left, const TValue& right) 
			{
			    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
		    };

		template <typename TPredicate>
		[[nodiscard]] bool ContainsIf(TPredicate&& predicate) const noexcept
		    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		TValue* begin() noexcept;
		const TValue* begin() const noexcept;
		TValue* end() noexcept;
		const TValue* end() const noexcept;

		ROXELL_FORCE_INLINE TValue& operator[](const usize index) noexcept;
		ROXELL_FORCE_INLINE const TValue& operator[](const usize index) const noexcept;

		[[nodiscard]] ROXELL_FORCE_INLINE bool Add(const TValue& value) noexcept
		    requires IsNothrowCopyConstructible<TValue>;
		[[nodiscard]] ROXELL_FORCE_INLINE bool Add(TValue&& value) noexcept;

		[[nodiscard]] bool Grow(const usize capacity) noexcept;

		void ReserveRequired(const usize capacity) noexcept;
		[[nodiscard]] bool Reserve(usize capacity) noexcept;
		[[nodiscard]] bool Resize(usize size) noexcept
		    requires IsNothrowDefaultConstructible<TValue>;

		// Grows to size without initializing the new elements; the caller writes every one of them
		// before reading. Shrinking behaves like ResizeRequired.
		[[nodiscard]] ROXELL_FORCE_INLINE bool ResizeUninitialized(const usize size) noexcept
		    requires IsTriviallyCopyable<TValue>;

		// Growth consumes aliased arguments before relocating the original elements.
		template <typename... TArguments>
		ROXELL_FORCE_INLINE TValue& EmplaceRequired(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments...>;

		ROXELL_FORCE_INLINE TValue& AddRequired(const TValue& value) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		ROXELL_FORCE_INLINE TValue& AddRequired(TValue&& value) noexcept;

		// Failure leaves the vector and aliased arguments unchanged.
		template <typename... TArguments>
		[[nodiscard]] ROXELL_FORCE_INLINE TValue* Emplace(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments...>;

		[[nodiscard]] bool Append(const Span<const TValue> source) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		void AppendRequired(const Span<const TValue> source) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		void ResizeRequired(const usize size) noexcept
		    requires IsNothrowDefaultConstructible<TValue>;

		// Predicates inspect original elements in order through const references, without structural re-entry.
		// Captured element references must survive compaction. Views before the first removed element survive.
		template <typename TPredicate>
		usize RemoveIf(TPredicate&& predicate) noexcept
		    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		// Invalidates the erased element, every element view at or after index, and the past-the-end iterator.
		void RemoveAt(const usize index) noexcept;

		// Invalidates the erased and previous last element views, plus past-the-end; other element views remain valid.
		void RemoveAtSwap(const usize index) noexcept;

		// Without growth, insertion invalidates views at and after index; an aliased rvalue is consumed first.
		TValue& InsertRequired(const usize index, const TValue& value) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		TValue& InsertRequired(const usize index, TValue&& value) noexcept;

		// Construct aliased arguments before moving existing elements or reallocating storage.
		template <typename... TArguments>
		TValue& EmplaceAtRequired(const usize index, TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments&&...>;

		[[nodiscard]] TValue* Insert(const usize index, const TValue& value) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		[[nodiscard]] TValue* Insert(const usize index, TValue&& value) noexcept;

		template <typename... TArguments>
		[[nodiscard]] TValue* EmplaceAt(const usize index, TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments...>;

		void PopBack() noexcept;

		void Clear() noexcept;

		void Reset() noexcept;

		// Success replaces output and invalidates all of its views. Failure leaves output unchanged.
		[[nodiscard]] bool CloneTo(MemoryResource& resource, SmallVector* const outSmallVector) const noexcept
		    requires IsNothrowCopyConstructible<TValue>;

	private:
		[[nodiscard]] ROXELL_NOINLINE bool tryGrowUninitialized(const usize size) noexcept
		    requires IsTriviallyCopyable<TValue>;

		template <typename... TArguments>
		TValue& insertValue(const usize index, TArguments&&... arguments) noexcept;

		TValue* inlineData() noexcept;
		const TValue* inlineData() const noexcept;

		bool isInline() const noexcept;

		static constexpr usize maximumCapacity() noexcept;

		void copyFrom(const SmallVector& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		void moveFrom(SmallVector& other) noexcept;

		usize growthCapacity(const usize required) const noexcept;

		[[nodiscard]] TValue* tryAllocate(const usize capacity) noexcept;

		TValue* allocate(const usize capacity) noexcept;

		void destroyRange(const usize first, const usize last) noexcept;

		void moveTo(TValue* destination) noexcept;

		void releaseStorage() noexcept;

		void replaceStorage(TValue* data, const usize capacity) noexcept;

		template <typename... TArguments> [[nodiscard]] TValue* tryEmplaceWithGrowth(TArguments&&... arguments) noexcept;

		template <typename... TArguments> TValue& addWithGrowth(TArguments&&... arguments) noexcept;

	private:
		alignas(TValue) Byte m_Inline[N == 0 ? 1 : N * sizeof(TValue)];
		TValue* m_Data = inlineData();
		usize m_Size = 0;
		usize m_Capacity = N;
		MemoryResource* m_Resource;
	};
}

namespace rox::core
{
	template <typename TValue, usize N>
	TValue& SmallVector<TValue, N>::At(usize index) noexcept
	{
		return (*this)[index];
	}

	template <typename TValue, usize N>
	TValue& SmallVector<TValue, N>::Front() noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::Front requires a non-empty range.");
		return (*this)[0];
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::Back() noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::Back requires a non-empty range.");
		return (*this)[m_Size - 1];
	}

	template <typename TValue, usize N>
	const TValue& SmallVector<TValue, N>::At(usize index) const noexcept
	{
		return (*this)[index];
	}

	template <typename TValue, usize N>
	const TValue& SmallVector<TValue, N>::Front() const noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::Front requires a non-empty range.");
		return (*this)[0];
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE const TValue& SmallVector<TValue, N>::Back() const noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::Back requires a non-empty range.");
		return (*this)[m_Size - 1];
	}

	template <typename TValue, usize N>
	SmallVector<TValue, N>::SmallVector(MemoryResource& resource) noexcept
	    : m_Resource(&resource)
	{
	}

	template <typename TValue, usize N>
	SmallVector<TValue, N>::~SmallVector() noexcept
	{
		destroyRange(0, m_Size);
		releaseStorage();
	}

	template <typename TValue, usize N>
	SmallVector<TValue, N>::SmallVector(const SmallVector& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	    : SmallVector(*other.m_Resource)
	{
		copyFrom(other);
	}

	template <typename TValue, usize N>
	SmallVector<TValue, N>& SmallVector<TValue, N>::operator=(const SmallVector& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
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
		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (other.m_Size <= m_Capacity)
			{
				core::CopyBytes(m_Data, other.Data(), other.m_Size * sizeof(TValue));
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

	template <typename TValue, usize N>
	SmallVector<TValue, N>& SmallVector<TValue, N>::AssignDisjoint(const SmallVector& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		if (this == &other)
		{
			return *this;
		}
		if constexpr (IsTriviallyCopyable<TValue>)
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
				core::ConstructAt(m_Data + m_Size, value);
				++m_Size;
			}
			return *this;
		}
	}

	template <typename TValue, usize N>
	SmallVector<TValue, N>::SmallVector(SmallVector&& other) noexcept
	    : SmallVector(*other.m_Resource)
	{
		moveFrom(other);
	}

	template <typename TValue, usize N>
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
				replacement.EmplaceRequired(core::Move(value));
			}
			Reset();
			other.Reset();
			moveFrom(replacement);
		}

		return *this;
	}

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::data() noexcept
	{
		return Data();
	}

	template <typename TValue, usize N>
	const TValue* SmallVector<TValue, N>::data() const noexcept
	{
		return Data();
	}

	template <typename TValue, usize N>
	usize SmallVector<TValue, N>::size() const noexcept
	{
		return Size();
	}

	template <typename TValue, usize N>
	bool SmallVector<TValue, N>::empty() const noexcept
	{
		return IsEmpty();
	}

	template <typename TValue, usize N>
	usize SmallVector<TValue, N>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename TValue, usize N>
	usize SmallVector<TValue, N>::Capacity() const noexcept
	{
		return m_Capacity;
	}

	template <typename TValue, usize N>
	bool SmallVector<TValue, N>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename TValue, usize N>
	MemoryResource& SmallVector<TValue, N>::GetResource() const noexcept
	{
		return *m_Resource;
	}

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::Data() noexcept
	{
		return m_Data;
	}

	template <typename TValue, usize N>
	const TValue* SmallVector<TValue, N>::Data() const noexcept
	{
		return m_Data;
	}

	template <typename TValue, usize N>
	Span<TValue> SmallVector<TValue, N>::View() noexcept
	{
		return {m_Data, m_Size};
	}

	template <typename TValue, usize N>
	Span<const TValue> SmallVector<TValue, N>::View() const noexcept
	{
		return {m_Data, m_Size};
	}

	template <typename TValue, usize N>
	template <typename TPredicate>
	ROXELL_FORCE_INLINE TValue* SmallVector<TValue, N>::FindIf(TPredicate&& predicate) noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		if (m_Size == 0)
		{
			return nullptr;
		}
		TValue* cursor = Data();
		constexpr usize BLOCK_SIZE = sizeof(TValue) <= 16 ? 8 : 4;
		usize remaining = m_Size;
		TValue* const blockEnd = cursor + (m_Size - m_Size % BLOCK_SIZE);
		TValue* const end = cursor + m_Size;
		while (BLOCK_SIZE == 8 ? cursor != blockEnd : remaining >= 4)
		{
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if constexpr (BLOCK_SIZE == 8)
			{
				if (core::Invoke(predicate, core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (core::Invoke(predicate, core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (core::Invoke(predicate, core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (core::Invoke(predicate, core::AsConst(*cursor)))
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
			if (core::Invoke(predicate, core::AsConst(*cursor)))
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

	template <typename TValue, usize N>
	template <typename TPredicate>
	ROXELL_FORCE_INLINE const TValue* SmallVector<TValue, N>::FindIf(TPredicate&& predicate) const noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		if (m_Size == 0)
		{
			return nullptr;
		}
		const TValue* cursor = Data();
		constexpr usize BLOCK_SIZE = sizeof(TValue) <= 16 ? 8 : 4;
		usize remaining = m_Size;
		const TValue* const blockEnd = cursor + (m_Size - m_Size % BLOCK_SIZE);
		const TValue* const end = cursor + m_Size;
		while (BLOCK_SIZE == 8 ? cursor != blockEnd : remaining >= 4)
		{
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			if constexpr (BLOCK_SIZE == 8)
			{
				if (core::Invoke(predicate, core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (core::Invoke(predicate, core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (core::Invoke(predicate, core::AsConst(*cursor)))
				{
					return cursor;
				}
				++cursor;
				if (core::Invoke(predicate, core::AsConst(*cursor)))
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
			if (core::Invoke(predicate, core::AsConst(*cursor)))
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

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::Find(const TValue& value) noexcept
	    requires requires(const TValue& left, const TValue& right) {
		    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
	    }
	{
		if constexpr (IsIntegral<TValue>)
		{
			if (m_Size == 0)
			{
				return nullptr;
			}
			auto* const first = Data();
			auto* const last = first + m_Size;
			auto* const found = core::Find(first, last, value);
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

	template <typename TValue, usize N>
	const TValue* SmallVector<TValue, N>::Find(const TValue& value) const noexcept
	    requires requires(const TValue& left, const TValue& right) {
		    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
	    }
	{
		if constexpr (IsIntegral<TValue>)
		{
			if (m_Size == 0)
			{
				return nullptr;
			}
			auto* const first = Data();
			auto* const last = first + m_Size;
			auto* const found = core::Find(first, last, value);
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

	template <typename TValue, usize N>
	bool SmallVector<TValue, N>::Contains(const TValue& value) const noexcept
	    requires requires(const TValue& left, const TValue& right) {
		    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
	    }
	{
		return Find(value) != nullptr;
	}

	template <typename TValue, usize N>
	template <typename TPredicate>
	bool SmallVector<TValue, N>::ContainsIf(TPredicate&& predicate) const noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		return FindIf(core::Forward<TPredicate>(predicate)) != nullptr;
	}

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::begin() noexcept
	{
		return m_Data;
	}

	template <typename TValue, usize N>
	const TValue* SmallVector<TValue, N>::begin() const noexcept
	{
		return m_Data;
	}

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::end() noexcept
	{
		return m_Size == 0 ? m_Data : m_Data + m_Size;
	}

	template <typename TValue, usize N>
	const TValue* SmallVector<TValue, N>::end() const noexcept
	{
		return m_Size == 0 ? m_Data : m_Data + m_Size;
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::operator[](const usize index) noexcept
	{
		ASSERT(index < m_Size, "SmallVector index must address a live element.");
		return m_Data[index];
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE const TValue& SmallVector<TValue, N>::operator[](const usize index) const noexcept
	{
		ASSERT(index < m_Size, "SmallVector index must address a live element.");
		return m_Data[index];
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE bool SmallVector<TValue, N>::Add(const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return Emplace(value) != nullptr;
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE bool SmallVector<TValue, N>::Add(TValue&& value) noexcept
	{
		return Emplace(core::Move(value)) != nullptr;
	}

	template <typename TValue, usize N>
	bool SmallVector<TValue, N>::Grow(const usize capacity) noexcept
	{
		if (capacity <= m_Capacity)
		{
			return true;
		}
		return capacity <= maximumCapacity() && Reserve(growthCapacity(capacity));
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::ReserveRequired(const usize capacity) noexcept
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

	template <typename TValue, usize N>
	bool SmallVector<TValue, N>::Reserve(usize capacity) noexcept
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

	template <typename TValue, usize N>
	bool SmallVector<TValue, N>::Resize(usize size) noexcept
	    requires IsNothrowDefaultConstructible<TValue>
	{
		if (!Reserve(size))
		{
			return false;
		}
		ResizeRequired(size);
		return true;
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE bool SmallVector<TValue, N>::ResizeUninitialized(const usize size) noexcept
	    requires IsTriviallyCopyable<TValue>
	{
		// Trivially copyable values have trivial destruction, including when shrinking.
		if (size <= m_Capacity)
		{
			m_Size = size;
			return true;
		}
		return tryGrowUninitialized(size);
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::EmplaceRequired(TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments...>
	{
		if (m_Size < m_Capacity)
		{
			TValue* const value = core::ConstructAt(m_Data + m_Size, core::Forward<TArguments>(arguments)...);
			++m_Size;
			return *value;
		}
		ASSERT(m_Size < maximumCapacity(), "SmallVector::EmplaceRequired requires representable growth.");
		return addWithGrowth(core::Forward<TArguments>(arguments)...);
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::AddRequired(const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return EmplaceRequired(value);
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE TValue& SmallVector<TValue, N>::AddRequired(TValue&& value) noexcept
	{
		return EmplaceRequired(core::Move(value));
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	ROXELL_FORCE_INLINE TValue* SmallVector<TValue, N>::Emplace(TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments...>
	{
		if (m_Size < m_Capacity)
		{
			TValue* const value = core::ConstructAt(m_Data + m_Size, core::Forward<TArguments>(arguments)...);
			++m_Size;
			return value;
		}
		return tryEmplaceWithGrowth(core::Forward<TArguments>(arguments)...);
	}

	template <typename TValue, usize N>
	bool SmallVector<TValue, N>::Append(const Span<const TValue> source) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		if (source.Size() > maximumCapacity() - m_Size)
		{
			return false;
		}
		if (source.Size() > m_Capacity - m_Size)
		{
			const usize capacity = growthCapacity(m_Size + source.Size());
			TValue* const data = tryAllocate(capacity);
			if (data == nullptr)
			{
				return false;
			}
			core::UninitializedCopyN(source.Data(), source.Size(), data + m_Size);
			moveTo(data);
			replaceStorage(data, capacity);
			m_Size += source.Size();
			return true;
		}
		AppendRequired(source);
		return true;
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::AppendRequired(const Span<const TValue> source) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		ASSERT(source.Size() <= maximumCapacity() - m_Size,
		       "SmallVector::AppendRequired size must fit its representable limit.");

		if (source.IsEmpty())
		{
			return;
		}

		const usize required = m_Size + source.Size();
		const bool bGrow = required > m_Capacity;
		const usize capacity = bGrow ? growthCapacity(required) : m_Capacity;
		TValue* const data = bGrow ? allocate(capacity) : m_Data;
		if constexpr (IsTriviallyCopyable<TValue>)
		{
			core::CopyBytes(data + m_Size, source.Data(), source.Size() * sizeof(TValue));
		}
		else
		{
			for (usize index = 0; index < source.Size(); ++index)
			{
				core::ConstructAt(data + m_Size + index, source[index]);
			}
		}
		if (bGrow)
		{
			moveTo(data);
			replaceStorage(data, capacity);
		}

		m_Size = required;
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::ResizeRequired(const usize size) noexcept
	    requires IsNothrowDefaultConstructible<TValue>
	{
		ASSERT(size <= maximumCapacity(), "SmallVector::ResizeRequired size must fit its representable limit.");

		if (size < m_Size)
		{
			destroyRange(size, m_Size);
			m_Size = size;
			return;
		}
		ReserveRequired(size);
		if constexpr (IsTrivial<TValue>)
		{
			if (size != m_Size)
			{
				core::ValueConstructN(m_Data + m_Size, size - m_Size);
			}
			m_Size = size;
		}
		else
		{
			while (m_Size < size)
			{
				core::ConstructAt(m_Data + m_Size);
				++m_Size;
			}
		}
	}

	template <typename TValue, usize N>
	template <typename TPredicate>
	usize SmallVector<TValue, N>::RemoveIf(TPredicate&& predicate) noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		TValue* const data = m_Data;
		const usize oldSize = m_Size;
		usize destination = 0;
		while (destination < oldSize && !core::Invoke(predicate, core::AsConst(data[destination])))
		{
			++destination;
		}
		if (destination == oldSize)
		{
			return 0;
		}
		core::DestroyAt(data + destination);
		// The first erased value leaves a gap, so every later survivor moves into a distinct slot.
		for (usize sourceIndex = destination + 1; sourceIndex < oldSize; ++sourceIndex)
		{
			if (core::Invoke(predicate, core::AsConst(data[sourceIndex])))
			{
				core::DestroyAt(data + sourceIndex);
			}
			else
			{
				core::detail::RelocateAtUnchecked(data + destination, data + sourceIndex);
				++destination;
			}
		}

		const usize removed = oldSize - destination;
		m_Size = destination;
		return removed;
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::RemoveAt(const usize index) noexcept
	{
		ASSERT(index < m_Size, "SmallVector::RemoveAt index must identify an initialized element.");
		core::DestroyAt(m_Data + index);

		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (index + 1 < m_Size)
			{
				core::MoveBytes(m_Data + index, m_Data + index + 1,
				                     (m_Size - index - 1) * sizeof(TValue));
				core::DestroyAt(m_Data + m_Size - 1);
			}
		}
		else
		{
			TValue* const data = m_Data;
			TValue* const last = data + m_Size;
			for (TValue* source = data + index + 1; source != last; ++source)
			{
				core::detail::RelocateAtUnchecked(source - 1, source);
			}
		}
		--m_Size;
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::RemoveAtSwap(const usize index) noexcept
	{
		ASSERT(index < m_Size, "SmallVector::RemoveAtSwap index must identify an initialized element.");
		const usize lastIndex = m_Size - 1;
		core::DestroyAt(m_Data + index);

		if (index != lastIndex)
		{
			core::detail::RelocateAtUnchecked(m_Data + index, m_Data + lastIndex);
		}
		--m_Size;
	}

	template <typename TValue, usize N>
	TValue& SmallVector<TValue, N>::InsertRequired(const usize index, const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return insertValue(index, value);
	}

	template <typename TValue, usize N>
	TValue& SmallVector<TValue, N>::InsertRequired(const usize index, TValue&& value) noexcept
	{
		return insertValue(index, core::Move(value));
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue& SmallVector<TValue, N>::EmplaceAtRequired(const usize index, TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments&&...>
	{
		return insertValue(index, core::Forward<TArguments>(arguments)...);
	}

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::Insert(const usize index, const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return EmplaceAt(index, value);
	}

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::Insert(const usize index, TValue&& value) noexcept
	{
		return EmplaceAt(index, core::Move(value));
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue* SmallVector<TValue, N>::EmplaceAt(const usize index, TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments...>
	{
		ASSERT(index <= m_Size, "SmallVector::EmplaceAt index must fit the initialized range including its end.");
		if (m_Size == maximumCapacity())
		{
			return nullptr;
		}
		if (m_Size < m_Capacity)
		{
			return core::AddressOf(insertValue(index, core::Forward<TArguments>(arguments)...));
		}
		const usize capacity = growthCapacity(m_Size + 1);
		TValue* const data = tryAllocate(capacity);
		if (data == nullptr)
		{
			return nullptr;
		}
		core::ConstructAt(data + index, core::Forward<TArguments>(arguments)...);
		for (usize source = 0; source < m_Size; ++source)
		{
			if constexpr (core::Relocation<TValue>::IsCustomized)
			{
				core::detail::RelocateAtUnchecked(data + source + (source >= index ? 1 : 0), m_Data + source);
			}
			else
			{
				core::ConstructAt(data + source + (source >= index ? 1 : 0), core::Move(m_Data[source]));
			}
		}
		replaceStorage(data, capacity);
		++m_Size;
		return m_Data + index;
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::PopBack() noexcept
	{
		ASSERT(m_Size != 0, "SmallVector::PopBack requires a live element.");
		core::DestroyAt(m_Data + m_Size - 1);
		--m_Size;
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::Clear() noexcept
	{
		destroyRange(0, m_Size);
		m_Size = 0;
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::Reset() noexcept
	{
		Clear();
		releaseStorage();
		m_Data = inlineData();
		m_Capacity = N;
	}

	template <typename TValue, usize N>
	bool SmallVector<TValue, N>::CloneTo(MemoryResource& resource, SmallVector* const outSmallVector) const noexcept
	    requires IsNothrowCopyConstructible<TValue>
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
		*outSmallVector = core::Move(clone);
		return true;
	}

	template <typename TValue, usize N>
	ROXELL_NOINLINE bool SmallVector<TValue, N>::tryGrowUninitialized(const usize size) noexcept
	    requires IsTriviallyCopyable<TValue>
	{
		if (!Reserve(size))
		{
			return false;
		}
		m_Size = size;
		return true;
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue& SmallVector<TValue, N>::insertValue(const usize index, TArguments&&... arguments) noexcept
	{
		ASSERT(index <= m_Size, "SmallVector::InsertRequired index must fit the initialized range including its end.");
		ASSERT(m_Size != maximumCapacity(), "SmallVector::InsertRequired cannot exceed its maximum representable capacity.");
		TValue pending(core::Forward<TArguments>(arguments)...);

		if (m_Size == m_Capacity)
		{
			ReserveRequired(growthCapacity(m_Size + 1));
		}

		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (index != m_Size)
			{
				core::MoveBytes(m_Data + index + 1, m_Data + index,
				                     (m_Size - index) * sizeof(TValue));
			}
		}
		else
		{
			TValue* const data = m_Data;
			TValue* const first = data + index;
			for (TValue* destination = data + m_Size; destination != first; --destination)
			{
				core::detail::RelocateAtUnchecked(destination, destination - 1);
			}
		}

		core::ConstructAt(m_Data + index, core::Move(pending));
		++m_Size;
		return m_Data[index];
	}

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::inlineData() noexcept
	{
		return N == 0 ? nullptr : reinterpret_cast<TValue*>(m_Inline);
	}

	template <typename TValue, usize N>
	const TValue* SmallVector<TValue, N>::inlineData() const noexcept
	{
		return N == 0 ? nullptr : reinterpret_cast<const TValue*>(m_Inline);
	}

	template <typename TValue, usize N>
	bool SmallVector<TValue, N>::isInline() const noexcept
	{
		return m_Data == inlineData();
	}

	template <typename TValue, usize N>
	constexpr usize SmallVector<TValue, N>::maximumCapacity() noexcept
	{
		return NumericLimits<usize>::max() / sizeof(TValue);
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::copyFrom(const SmallVector& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		ReserveRequired(other.m_Size);

		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (other.m_Size != 0)
			{
				core::CopyBytes(m_Data, other.Data(), other.m_Size * sizeof(TValue));
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

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::moveFrom(SmallVector& other) noexcept
	{
		if (other.isInline())
		{
			other.moveTo(m_Data);
			m_Size = other.m_Size;
			if constexpr (core::Relocation<TValue>::IsCustomized)
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
			m_Data = core::Exchange(other.m_Data, other.inlineData());
			m_Size = core::Exchange(other.m_Size, 0);
			m_Capacity = core::Exchange(other.m_Capacity, N);
		}
	}

	template <typename TValue, usize N>
	usize SmallVector<TValue, N>::growthCapacity(const usize required) const noexcept
	{
		const usize doubled = m_Capacity > maximumCapacity() / 2 ? maximumCapacity() : m_Capacity * 2;
		return core::Max(required, core::Max(usize{1}, doubled));
	}

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::tryAllocate(const usize capacity) noexcept
	{
		if (capacity == 0 || capacity > maximumCapacity())
		{
			return nullptr;
		}
		return static_cast<TValue*>(m_Resource->Allocate({capacity * sizeof(TValue), alignof(TValue)}));
	}

	template <typename TValue, usize N>
	TValue* SmallVector<TValue, N>::allocate(const usize capacity) noexcept
	{
		return static_cast<TValue*>(m_Resource->AllocateRequired({capacity * sizeof(TValue), alignof(TValue)}));
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::destroyRange(const usize first, const usize last) noexcept
	{
		if constexpr (!IsTriviallyDestructible<TValue>)
		{
			for (usize index = first; index < last; ++index)
			{
				core::DestroyAt(m_Data + index);
			}
		}
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::moveTo(TValue* destination) noexcept
	{
		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (m_Size != 0)
			{
				core::CopyBytes(destination, m_Data, m_Size * sizeof(TValue));
			}
		}
		else
		{
			for (usize index = 0; index < m_Size; ++index)
			{
				if constexpr (core::Relocation<TValue>::IsCustomized)
				{
					core::detail::RelocateAtUnchecked(destination + index, m_Data + index);
				}
				else
				{
					core::ConstructAt(destination + index, core::Move(m_Data[index]));
				}
			}
		}
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::releaseStorage() noexcept
	{
		if (!isInline())
		{
			m_Resource->Deallocate(m_Data, {m_Capacity * sizeof(TValue), alignof(TValue)});
		}
	}

	template <typename TValue, usize N>
	void SmallVector<TValue, N>::replaceStorage(TValue* data, const usize capacity) noexcept
	{
		if constexpr (!core::Relocation<TValue>::IsCustomized)
		{
			destroyRange(0, m_Size);
		}
		releaseStorage();
		m_Data = data;
		m_Capacity = capacity;
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue* SmallVector<TValue, N>::tryEmplaceWithGrowth(TArguments&&... arguments) noexcept
	{
		if (m_Size == maximumCapacity())
		{
			return nullptr;
		}
		const usize capacity = growthCapacity(m_Size + 1);
		TValue* const data = tryAllocate(capacity);
		if (data == nullptr)
		{
			return nullptr;
		}
		TValue* const value = core::ConstructAt(data + m_Size, core::Forward<TArguments>(arguments)...);
		moveTo(data);
		replaceStorage(data, capacity);
		++m_Size;
		return value;
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue& SmallVector<TValue, N>::addWithGrowth(TArguments&&... arguments) noexcept
	{
		const usize capacity = growthCapacity(m_Size + 1);
		TValue* const data = allocate(capacity);
		TValue* const value = core::ConstructAt(data + m_Size, core::Forward<TArguments>(arguments)...);
		moveTo(data);
		replaceStorage(data, capacity);
		++m_Size;
		return *value;
	}
}
