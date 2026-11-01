#pragma once

#include <Assertion.h>
#include <Algorithms.hpp>
#include <Vocabulary.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <CompilerAttributes.h>
#include <MemoryResource.h>
#include <Span.hpp>
#include <SystemMemoryResource.h>

#include <cstddef>
#include <concepts>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// The resource must outlive the array. Reallocation invalidates all views, iterators, pointers, and references.
	template <typename TValue> class Array
	{
		static_assert(IsNothrowMoveConstructible<TValue>);
		static_assert(IsNothrowDestructible<TValue>);

	public:
		[[nodiscard]] TValue& At(usize index) noexcept;

		[[nodiscard]] TValue& Front() noexcept;

		[[nodiscard]] const TValue& At(usize index) const noexcept;

		[[nodiscard]] const TValue& Front() const noexcept;

		Array() noexcept;

		explicit Array(MemoryResource& resource) noexcept;

		Array(InitializerList<TValue> values, MemoryResource& resource) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		Array(InitializerList<TValue> values) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		Array(const Array& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		Array(Array&& other) noexcept;

		~Array();

		// Assignment invalidates destination views and retains its resource. Empty copies retain spare capacity.
		// Nontrivial copies are staged before releasing destination-owned objects, which may own the source.
		// Move assignment also invalidates every source view.
		Array& operator=(const Array& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		// Except for self-assignment, the ownership graphs must be disjoint: clearing this array must not
		// destroy the source or any object its copies require. Reuses capacity and invalidates destination views.
		Array& AssignDisjoint(const Array& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		Array& operator=(Array&& other) noexcept;

		// These observers also make the resource-owned storage a standard contiguous range.
		[[nodiscard]] TValue* data() noexcept;
		[[nodiscard]] const TValue* data() const noexcept;
		[[nodiscard]] usize size() const noexcept;
		[[nodiscard]] bool empty() const noexcept;

		[[nodiscard]] TValue* Data() noexcept;

		[[nodiscard]] const TValue* Data() const noexcept;

		[[nodiscard]] usize Size() const noexcept;

		[[nodiscard]] usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		[[nodiscard]] Span<TValue> View() noexcept;

		[[nodiscard]] Span<const TValue> View() const noexcept;

		[[nodiscard]] ROXELL_FORCE_INLINE TValue& operator[](const usize index) noexcept;

		[[nodiscard]] ROXELL_FORCE_INLINE const TValue& operator[](const usize index) const noexcept;

		[[nodiscard]] ROXELL_FORCE_INLINE TValue& Back() noexcept;

		[[nodiscard]] ROXELL_FORCE_INLINE const TValue& Back() const noexcept;

		[[nodiscard]] TValue* begin() noexcept;

		[[nodiscard]] const TValue* begin() const noexcept;

		[[nodiscard]] TValue* end() noexcept;

		[[nodiscard]] const TValue* end() const noexcept;

		// Predicates inspect const elements in order and stop at the first match; returned pointers are borrowed.
		template <typename TPredicate>
		[[nodiscard]] ROXELL_FORCE_INLINE TValue* FindIf(TPredicate&& predicate) noexcept
		    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		template <typename TPredicate>
		[[nodiscard]] ROXELL_FORCE_INLINE const TValue* FindIf(TPredicate&& predicate) const noexcept
		    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		[[nodiscard]] TValue* Find(const TValue& value) noexcept
		    requires requires(const TValue& left, const TValue& right) {
			    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
		    };

		[[nodiscard]] const TValue* Find(const TValue& value) const noexcept
		    requires requires(const TValue& left, const TValue& right) {
			    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
		    };

		[[nodiscard]] bool Contains(const TValue& value) const noexcept
		    requires requires(const TValue& left, const TValue& right) {
			    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
		    };

		template <typename TPredicate>
		[[nodiscard]] bool ContainsIf(TPredicate&& predicate) const noexcept
		    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		// Reserve and ReserveRequired preserve views when capacity is sufficient; reallocation invalidates all views.
		[[nodiscard]] bool Reserve(const usize minimumCapacity) noexcept;

		// Recoverable geometric reservation for transactions that must reserve before mutating.
		[[nodiscard]] bool Grow(const usize minimumCapacity) noexcept;

		// Grows to size without initializing the new elements; the caller writes every one of them
		// before reading. Shrinking behaves like ResizeRequired.
		[[nodiscard]] bool ResizeUninitialized(const usize size) noexcept
		    requires IsTriviallyCopyable<TValue>;


		void ReserveRequired(const usize minimumCapacity) noexcept;

		// Add and AddRequired preserve existing element views unless they reallocate. The past-the-end iterator changes.
		[[nodiscard]] ROXELL_FORCE_INLINE bool Add(const TValue& value) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		[[nodiscard]] ROXELL_FORCE_INLINE bool Add(TValue&& value) noexcept;

		ROXELL_FORCE_INLINE TValue& AddRequired(const TValue& value) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		ROXELL_FORCE_INLINE TValue& AddRequired(TValue&& value) noexcept;

		// Arguments may refer to existing elements; growth consumes them before relocating that storage.
		template <typename... TArguments>
		ROXELL_FORCE_INLINE TValue& EmplaceRequired(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments...>;

		// Failure leaves the array and aliased arguments unchanged.
		template <typename... TArguments>
		[[nodiscard]] ROXELL_FORCE_INLINE TValue* Emplace(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments...>;

		[[nodiscard]] bool Append(const Span<const TValue> source) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		// Source may be any initialized self-subrange. Existing views survive unless storage grows.
		void AppendRequired(const Span<const TValue> source) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		// Growth preserves existing views unless it reallocates; shrinking invalidates only the removed suffix.
		void ResizeRequired(const usize size) noexcept
		    requires IsNothrowDefaultConstructible<TValue>;

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

		// Predicates inspect original elements in order through const references, without structural re-entry.
		// Captured element references must survive compaction. Views before the first removed element survive.
		template <typename TPredicate>
		usize RemoveIf(TPredicate&& predicate) noexcept
		    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>;

		// Growing preserves existing element views unless it reallocates. Shrinking invalidates the removed suffix.
		// Any size change invalidates the past-the-end iterator.
		[[nodiscard]] bool Resize(const usize size) noexcept
		    requires IsNothrowDefaultConstructible<TValue>;

		// Invalidates the erased element, every element view at or after index, and the past-the-end iterator.
		void RemoveAt(const usize index) noexcept;

		// Invalidates the erased and previous last element views, plus past-the-end; other element views remain valid.
		void RemoveAtSwap(const usize index) noexcept;

		// Invalidates the removed last element and the past-the-end iterator.
		ROXELL_FORCE_INLINE void PopBack() noexcept;

		// Invalidates every element view and past-the-end but preserves the allocation and its capacity.
		void Clear() noexcept;

		// Invalidates every view and releases the allocation.
		void Reset() noexcept;

		// Success replaces output and invalidates all of its views. Failure leaves output unchanged.
		[[nodiscard]] bool CloneTo(MemoryResource& resource, Array* const outArray) const noexcept
		    requires IsNothrowCopyConstructible<TValue>;

	private:
		[[nodiscard]] static constexpr usize maximumCapacity() noexcept;

		template <typename... TArguments>
		TValue& insertValue(const usize index, TArguments&&... arguments) noexcept;

		[[nodiscard]] usize growthCapacity(const usize minimumCapacity) const noexcept;

		[[nodiscard]] static constexpr usize byteCount(const usize capacity) noexcept;

		[[nodiscard]] TValue* allocate(const usize capacity) noexcept;

		[[nodiscard]] TValue* allocateRequired(const usize capacity) noexcept;

		void moveTo(TValue* destination) noexcept;

		[[nodiscard]] bool tryReallocate(const usize capacity) noexcept;

		void reallocate(const usize capacity) noexcept;

		template <typename... TArguments> [[nodiscard]] ROXELL_NOINLINE TValue* tryEmplaceWithGrowth(TArguments&&... arguments) noexcept;

		template <typename TArgument> [[nodiscard]] ROXELL_NOINLINE bool tryAddWithGrowth(TArgument&& value) noexcept;

		template <typename... TArguments> ROXELL_NOINLINE TValue& addWithGrowth(TArguments&&... arguments) noexcept;

		void replaceStorage(TValue* newData, const usize newCapacity) noexcept;

		void copyFrom(const Array& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		void destroyRange(const usize first, const usize last) noexcept;

		void releaseStorage() noexcept;

		void stealStorage(Array& source) noexcept;

	private:
		TValue* m_Data = nullptr;
		usize m_Size = 0;
		usize m_Capacity = 0;
		MemoryResource* m_Resource = nullptr;
	};
} // namespace rox::core

namespace rox::core
{
	template <typename TValue>
	TValue& Array<TValue>::At(usize index) noexcept
	{
		return (*this)[index];
	}

	template <typename TValue>
	TValue& Array<TValue>::Front() noexcept
	{
		ASSERT(m_Size != 0, "Array::Front requires a non-empty range.");
		return (*this)[0];
	}

	template <typename TValue>
	const TValue& Array<TValue>::At(usize index) const noexcept
	{
		return (*this)[index];
	}

	template <typename TValue>
	const TValue& Array<TValue>::Front() const noexcept
	{
		ASSERT(m_Size != 0, "Array::Front requires a non-empty range.");
		return (*this)[0];
	}

	template <typename TValue>
	Array<TValue>::Array() noexcept
	    : m_Resource(&SystemMemoryResource::Get())
	{
	}

	template <typename TValue>
	Array<TValue>::Array(MemoryResource& resource) noexcept
	    : m_Resource(&resource)
	{
	}

	template <typename TValue>
	Array<TValue>::Array(InitializerList<TValue> values, MemoryResource& resource) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	    : Array(resource)
	{
		ReserveRequired(values.size());

		for (const auto& value : values)
		{
			AddRequired(value);
		}
	}

	template <typename TValue>
	Array<TValue>::Array(InitializerList<TValue> values) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	    : Array(values, SystemMemoryResource::Get())
	{
	}

	template <typename TValue>
	Array<TValue>::Array(const Array& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	    : m_Resource(other.m_Resource)
	{
		copyFrom(other);
	}

	template <typename TValue>
	Array<TValue>::Array(Array&& other) noexcept
	    : m_Data(other.m_Data)
	    , m_Size(other.m_Size)
	    , m_Capacity(other.m_Capacity)
	    , m_Resource(other.m_Resource)
	{
		other.m_Data = nullptr;
		other.m_Size = 0;
		other.m_Capacity = 0;
	}

	template <typename TValue>
	Array<TValue>::~Array()
	{
		releaseStorage();
	}

	template <typename TValue>
	Array<TValue>& Array<TValue>::operator=(const Array& other) noexcept
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
				core::CopyBytes(m_Data, other.m_Data, other.m_Size * sizeof(TValue));
				m_Size = other.m_Size;
				return *this;
			}
		}

		Array temporary(*m_Resource);
		temporary.copyFrom(other);
		releaseStorage();
		stealStorage(temporary);
		return *this;
	}

	template <typename TValue>
	Array<TValue>& Array<TValue>::AssignDisjoint(const Array& other) noexcept
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

	template <typename TValue>
	Array<TValue>& Array<TValue>::operator=(Array&& other) noexcept
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

		Array replacement(*m_Resource);
		replacement.ReserveRequired(other.m_Size);

		for (TValue& value : other)
		{
			replacement.AddRequired(core::Move(value));
		}
		releaseStorage();
		stealStorage(replacement);
		other.Reset();
		return *this;
	}

	template <typename TValue>
	TValue* Array<TValue>::data() noexcept
	{
		return Data();
	}

	template <typename TValue>
	const TValue* Array<TValue>::data() const noexcept
	{
		return Data();
	}

	template <typename TValue>
	usize Array<TValue>::size() const noexcept
	{
		return Size();
	}

	template <typename TValue>
	bool Array<TValue>::empty() const noexcept
	{
		return IsEmpty();
	}

	template <typename TValue>
	TValue* Array<TValue>::Data() noexcept
	{
		return m_Data;
	}

	template <typename TValue>
	const TValue* Array<TValue>::Data() const noexcept
	{
		return m_Data;
	}

	template <typename TValue>
	usize Array<TValue>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename TValue>
	usize Array<TValue>::Capacity() const noexcept
	{
		return m_Capacity;
	}

	template <typename TValue>
	bool Array<TValue>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename TValue>
	MemoryResource& Array<TValue>::GetResource() const noexcept
	{
		ASSERT(m_Resource != nullptr, "Array must retain the resource that owns its storage.");
		return *m_Resource;
	}

	template <typename TValue>
	Span<TValue> Array<TValue>::View() noexcept
	{
		return {m_Data, m_Size};
	}

	template <typename TValue>
	Span<const TValue> Array<TValue>::View() const noexcept
	{
		return {m_Data, m_Size};
	}

	template <typename TValue>
	ROXELL_FORCE_INLINE TValue& Array<TValue>::operator[](const usize index) noexcept
	{
		ASSERT(index < m_Size, "Array index must be within the initialized element range.");
		return m_Data[index];
	}

	template <typename TValue>
	ROXELL_FORCE_INLINE const TValue& Array<TValue>::operator[](const usize index) const noexcept
	{
		ASSERT(index < m_Size, "Array index must be within the initialized element range.");
		return m_Data[index];
	}

	template <typename TValue>
	ROXELL_FORCE_INLINE TValue& Array<TValue>::Back() noexcept
	{
		ASSERT(m_Size != 0, "Array::Back requires a non-empty array.");
		return m_Data[m_Size - 1];
	}

	template <typename TValue>
	ROXELL_FORCE_INLINE const TValue& Array<TValue>::Back() const noexcept
	{
		ASSERT(m_Size != 0, "Array::Back requires a non-empty array.");
		return m_Data[m_Size - 1];
	}

	template <typename TValue>
	TValue* Array<TValue>::begin() noexcept
	{
		return m_Data;
	}

	template <typename TValue>
	const TValue* Array<TValue>::begin() const noexcept
	{
		return m_Data;
	}

	template <typename TValue>
	TValue* Array<TValue>::end() noexcept
	{
		return m_Data == nullptr ? nullptr : m_Data + m_Size;
	}

	template <typename TValue>
	const TValue* Array<TValue>::end() const noexcept
	{
		return m_Data == nullptr ? nullptr : m_Data + m_Size;
	}

	template <typename TValue>
	template <typename TPredicate>
	ROXELL_FORCE_INLINE TValue* Array<TValue>::FindIf(TPredicate&& predicate) noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		if (m_Size == 0)
		{
			return nullptr;
		}
		TValue* cursor = Data();
		usize remaining = m_Size;
		while (remaining >= 4)
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
			remaining -= 4;
		}
		while (remaining != 0)
		{
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			--remaining;
		}
		return nullptr;
	}

	template <typename TValue>
	template <typename TPredicate>
	ROXELL_FORCE_INLINE const TValue* Array<TValue>::FindIf(TPredicate&& predicate) const noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		if (m_Size == 0)
		{
			return nullptr;
		}
		const TValue* cursor = Data();
		usize remaining = m_Size;
		while (remaining >= 4)
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
			remaining -= 4;
		}
		while (remaining != 0)
		{
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
			--remaining;
		}
		return nullptr;
	}

	template <typename TValue>
	TValue* Array<TValue>::Find(const TValue& value) noexcept
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

	template <typename TValue>
	const TValue* Array<TValue>::Find(const TValue& value) const noexcept
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

	template <typename TValue>
	bool Array<TValue>::Contains(const TValue& value) const noexcept
	    requires requires(const TValue& left, const TValue& right) {
		    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
	    }
	{
		return Find(value) != nullptr;
	}

	template <typename TValue>
	template <typename TPredicate>
	bool Array<TValue>::ContainsIf(TPredicate&& predicate) const noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		return FindIf(core::Forward<TPredicate>(predicate)) != nullptr;
	}

	template <typename TValue>
	bool Array<TValue>::Reserve(const usize minimumCapacity) noexcept
	{
		if (minimumCapacity <= m_Capacity)
		{
			return true;
		}

		return tryReallocate(minimumCapacity);
	}

	template <typename TValue>
	bool Array<TValue>::Grow(const usize minimumCapacity) noexcept
	{
		if (minimumCapacity <= m_Capacity)
		{
			return true;
		}

		if (minimumCapacity > maximumCapacity())
		{
			return false;
		}

		return tryReallocate(growthCapacity(minimumCapacity));
	}

	template <typename TValue>
	bool Array<TValue>::ResizeUninitialized(const usize size) noexcept
	    requires IsTriviallyCopyable<TValue>
	{
		if (size < m_Size)
		{
			destroyRange(size, m_Size);
			m_Size = size;
			return true;
		}

		if (!Reserve(size))
		{
			return false;
		}

		m_Size = size;
		return true;
	}

	template <typename TValue>
	void Array<TValue>::ReserveRequired(const usize minimumCapacity) noexcept
	{
		if (minimumCapacity <= m_Capacity)
		{
			return;
		}
		reallocate(minimumCapacity);
	}

	template <typename TValue>
	ROXELL_FORCE_INLINE bool Array<TValue>::Add(const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		if (m_Size < m_Capacity)
		{
			core::ConstructAt(m_Data + m_Size, value);
			++m_Size;
			return true;
		}

		if (m_Size == maximumCapacity())
		{
			return false;
		}

		return tryAddWithGrowth(value);
	}

	template <typename TValue>
	ROXELL_FORCE_INLINE bool Array<TValue>::Add(TValue&& value) noexcept
	{
		if (m_Size < m_Capacity)
		{
			core::ConstructAt(m_Data + m_Size, core::Move(value));
			++m_Size;
			return true;
		}

		if (m_Size == maximumCapacity())
		{
			return false;
		}

		return tryAddWithGrowth(core::Move(value));
	}

	template <typename TValue>
	ROXELL_FORCE_INLINE TValue& Array<TValue>::AddRequired(const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		if (m_Size < m_Capacity)
		{
			core::ConstructAt(m_Data + m_Size, value);
			return m_Data[m_Size++];
		}

		ASSERT(m_Size != maximumCapacity(), "Array cannot grow beyond its maximum representable capacity.");
		return addWithGrowth(value);
	}

	template <typename TValue>
	ROXELL_FORCE_INLINE TValue& Array<TValue>::AddRequired(TValue&& value) noexcept
	{
		if (m_Size < m_Capacity)
		{
			core::ConstructAt(m_Data + m_Size, core::Move(value));
			return m_Data[m_Size++];
		}

		ASSERT(m_Size != maximumCapacity(), "Array cannot grow beyond its maximum representable capacity.");
		return addWithGrowth(core::Move(value));
	}

	template <typename TValue>
	template <typename... TArguments>
	ROXELL_FORCE_INLINE TValue& Array<TValue>::EmplaceRequired(TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments...>
	{
		if (m_Size < m_Capacity)
		{
			core::ConstructAt(m_Data + m_Size, core::Forward<TArguments>(arguments)...);
			return m_Data[m_Size++];
		}

		ASSERT(m_Size != maximumCapacity(), "Array::EmplaceRequired cannot exceed its maximum representable capacity.");
		return addWithGrowth(core::Forward<TArguments>(arguments)...);
	}

	template <typename TValue>
	template <typename... TArguments>
	ROXELL_FORCE_INLINE TValue* Array<TValue>::Emplace(TArguments&&... arguments) noexcept
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

	template <typename TValue>
	bool Array<TValue>::Append(const Span<const TValue> source) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		if (source.Size() > maximumCapacity() - m_Size)
		{
			return false;
		}
		if (source.Size() > m_Capacity - m_Size)
		{
			const usize capacity = growthCapacity(m_Size + source.Size());
			TValue* const data = allocate(capacity);
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

	template <typename TValue>
	void Array<TValue>::AppendRequired(const Span<const TValue> source) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		ASSERT(source.Size() <= maximumCapacity() - m_Size,
		       "Array::AppendRequired size must fit its maximum representable capacity.");
		const usize newSize = m_Size + source.Size();

		if (source.IsEmpty())
		{
			return;
		}

		if (newSize > m_Capacity)
		{
			const usize newCapacity = growthCapacity(newSize);
			TValue* const newData = allocateRequired(newCapacity);
			core::UninitializedCopyN(source.Data(), source.Size(), newData + m_Size);
			moveTo(newData);
			replaceStorage(newData, newCapacity);
			m_Size = newSize;
			return;
		}

		if constexpr (IsTriviallyCopyable<TValue>)
		{
			core::UninitializedCopyN(source.Data(), source.Size(), m_Data + m_Size);
			m_Size = newSize;
		}
		else
		{
			for (usize index = 0; index < source.Size(); ++index)
			{
				core::ConstructAt(m_Data + m_Size, source[index]);
				++m_Size;
			}
		}
	}

	template <typename TValue>
	void Array<TValue>::ResizeRequired(const usize size) noexcept
	    requires IsNothrowDefaultConstructible<TValue>
	{
		ASSERT(size <= maximumCapacity(), "Array::ResizeRequired size must fit its maximum representable capacity.");

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

	template <typename TValue>
	TValue& Array<TValue>::InsertRequired(const usize index, const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return insertValue(index, value);
	}

	template <typename TValue>
	TValue& Array<TValue>::InsertRequired(const usize index, TValue&& value) noexcept
	{
		return insertValue(index, core::Move(value));
	}

	template <typename TValue>
	template <typename... TArguments>
	TValue& Array<TValue>::EmplaceAtRequired(const usize index, TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments&&...>
	{
		return insertValue(index, core::Forward<TArguments>(arguments)...);
	}

	template <typename TValue>
	TValue* Array<TValue>::Insert(const usize index, const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return EmplaceAt(index, value);
	}

	template <typename TValue>
	TValue* Array<TValue>::Insert(const usize index, TValue&& value) noexcept
	{
		return EmplaceAt(index, core::Move(value));
	}

	template <typename TValue>
	template <typename... TArguments>
	TValue* Array<TValue>::EmplaceAt(const usize index, TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments...>
	{
		ASSERT(index <= m_Size, "Array::EmplaceAt index must fit the initialized range including its end.");
		if (m_Size == maximumCapacity())
		{
			return nullptr;
		}
		if (m_Size < m_Capacity)
		{
			return core::AddressOf(insertValue(index, core::Forward<TArguments>(arguments)...));
		}
		const usize capacity = growthCapacity(m_Size + 1);
		TValue* const data = allocate(capacity);
		if (data == nullptr)
		{
			return nullptr;
		}
		core::ConstructAt(data + index, core::Forward<TArguments>(arguments)...);
		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (index != 0)
			{
				core::CopyBytes(data, m_Data, index * sizeof(TValue));
			}
			if (index != m_Size)
			{
				core::CopyBytes(data + index + 1, m_Data + index, (m_Size - index) * sizeof(TValue));
			}
		}
		else
		{
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
		}
		replaceStorage(data, capacity);
		++m_Size;
		return m_Data + index;
	}

	template <typename TValue>
	template <typename TPredicate>
	usize Array<TValue>::RemoveIf(TPredicate&& predicate) noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		usize destination = 0;

		for (usize sourceIndex = 0; sourceIndex < m_Size; ++sourceIndex)
		{
			if (core::Invoke(predicate, core::AsConst(m_Data[sourceIndex])))
			{
				core::DestroyAt(m_Data + sourceIndex);
			}
			else
			{
				if (destination != sourceIndex)
				{
					core::detail::RelocateAtUnchecked(m_Data + destination, m_Data + sourceIndex);
				}
				++destination;
			}
		}

		const usize removed = m_Size - destination;
		m_Size = destination;
		return removed;
	}

	template <typename TValue>
	bool Array<TValue>::Resize(const usize size) noexcept
	    requires IsNothrowDefaultConstructible<TValue>
	{
		if (size < m_Size)
		{
			destroyRange(size, m_Size);
			m_Size = size;
			return true;
		}

		if (!Reserve(size))
		{
			return false;
		}

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

		return true;
	}

	template <typename TValue>
	void Array<TValue>::RemoveAt(const usize index) noexcept
	{
		ASSERT(index < m_Size, "Array::RemoveAt index must identify an initialized element.");
		core::DestroyAt(m_Data + index);

		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (index + 1 < m_Size)
			{
				core::MoveBytes(m_Data + index, m_Data + index + 1, (m_Size - index - 1) * sizeof(TValue));
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

	template <typename TValue>
	void Array<TValue>::RemoveAtSwap(const usize index) noexcept
	{
		ASSERT(index < m_Size, "Array::RemoveAtSwap index must identify an initialized element.");
		const usize lastIndex = m_Size - 1;
		core::DestroyAt(m_Data + index);

		if (index != lastIndex)
		{
			core::detail::RelocateAtUnchecked(m_Data + index, m_Data + lastIndex);
		}
		--m_Size;
	}

	template <typename TValue>
	ROXELL_FORCE_INLINE void Array<TValue>::PopBack() noexcept
	{
		ASSERT(m_Size != 0, "Array::PopBack requires a non-empty array.");
		core::DestroyAt(m_Data + m_Size - 1);
		--m_Size;
	}

	template <typename TValue>
	void Array<TValue>::Clear() noexcept
	{
		destroyRange(0, m_Size);
		m_Size = 0;
	}

	template <typename TValue>
	void Array<TValue>::Reset() noexcept
	{
		releaseStorage();
	}

	template <typename TValue>
	bool Array<TValue>::CloneTo(MemoryResource& resource, Array* const outArray) const noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		ASSERT(outArray != nullptr, "Array::CloneTo requires a valid output pointer.");
		ASSERT(&outArray->GetResource() == &resource,
		       "Array::CloneTo output must use the requested memory resource.");
		Array clone(resource);

		if (!clone.Reserve(m_Size))
		{
			return false;
		}
		clone.AppendRequired(View());
		*outArray = core::Move(clone);
		return true;
	}

	template <typename TValue>
	constexpr usize Array<TValue>::maximumCapacity() noexcept
	{
		return NumericLimits<usize>::max() / sizeof(TValue);
	}

	template <typename TValue>
	template <typename... TArguments>
	TValue& Array<TValue>::insertValue(const usize index, TArguments&&... arguments) noexcept
	{
		ASSERT(index <= m_Size, "Array::InsertRequired index must fit the initialized range including its end.");
		ASSERT(m_Size != maximumCapacity(), "Array::InsertRequired cannot exceed its maximum representable capacity.");
		TValue pending(core::Forward<TArguments>(arguments)...);

		if (m_Size == m_Capacity)
		{
			ReserveRequired(growthCapacity(m_Size + 1));
		}

		if constexpr (IsTriviallyCopyable<TValue>)
		{
			// Keep short shifts inline instead of paying a byte-move call for a few elements.
			const usize shiftedBytes = (m_Size - index) * sizeof(TValue);
			if (shiftedBytes > 256)
			{
				core::MoveBytes(m_Data + index + 1, m_Data + index, shiftedBytes);
			}
			else
			{
				for (usize destination = m_Size; destination > index; --destination)
				{
					core::detail::RelocateAtUnchecked(m_Data + destination, m_Data + destination - 1);
				}
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

	template <typename TValue>
	usize Array<TValue>::growthCapacity(const usize minimumCapacity) const noexcept
	{
		ASSERT(minimumCapacity <= maximumCapacity(),
		       "Array growth request must fit its maximum representable capacity.");
		usize capacity = m_Capacity == 0 ? 4 : m_Capacity + (m_Capacity / 2) + 1;

		if (capacity < m_Capacity || capacity > maximumCapacity() || capacity < minimumCapacity)
		{
			capacity = minimumCapacity;
		}

		return capacity;
	}

	template <typename TValue>
	constexpr usize Array<TValue>::byteCount(const usize capacity) noexcept
	{
		return capacity * sizeof(TValue);
	}

	template <typename TValue>
	TValue* Array<TValue>::allocate(const usize capacity) noexcept
	{
		if (capacity == 0 || capacity > maximumCapacity())
		{
			return nullptr;
		}

		return static_cast<TValue*>(m_Resource->Allocate({byteCount(capacity), alignof(TValue)}));
	}

	template <typename TValue>
	TValue* Array<TValue>::allocateRequired(const usize capacity) noexcept
	{
		ASSERT(capacity != 0 && capacity <= maximumCapacity(),
		       "Mandatory Array allocation requires a valid non-zero capacity.");
		return static_cast<TValue*>(m_Resource->AllocateRequired({byteCount(capacity), alignof(TValue)}));
	}

	template <typename TValue>
	void Array<TValue>::moveTo(TValue* destination) noexcept
	{
		ASSERT(destination != nullptr || m_Size == 0, "Moving Array elements requires valid destination storage.");
		ASSERT(m_Data != nullptr || m_Size == 0, "A non-empty Array must own valid source storage.");
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

	template <typename TValue>
	bool Array<TValue>::tryReallocate(const usize capacity) noexcept
	{
		TValue* const newData = allocate(capacity);

		if (newData == nullptr)
		{
			return false;
		}
		moveTo(newData);
		replaceStorage(newData, capacity);
		return true;
	}

	template <typename TValue>
	void Array<TValue>::reallocate(const usize capacity) noexcept
	{
		TValue* const newData = allocateRequired(capacity);
		moveTo(newData);
		replaceStorage(newData, capacity);
	}

	template <typename TValue>
	template <typename... TArguments>
	ROXELL_NOINLINE TValue* Array<TValue>::tryEmplaceWithGrowth(TArguments&&... arguments) noexcept
	{
		if (m_Size == maximumCapacity())
		{
			return nullptr;
		}
		const usize capacity = growthCapacity(m_Size + 1);
		TValue* const data = allocate(capacity);
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

	template <typename TValue>
	template <typename TArgument>
	ROXELL_NOINLINE bool Array<TValue>::tryAddWithGrowth(TArgument&& value) noexcept
	{
		const usize newCapacity = growthCapacity(m_Size + 1);
		TValue* const newData = allocate(newCapacity);

		if (newData == nullptr)
		{
			return false;
		}

		core::ConstructAt(newData + m_Size, core::Forward<TArgument>(value));
		moveTo(newData);
		replaceStorage(newData, newCapacity);
		++m_Size;
		return true;
	}

	template <typename TValue>
	template <typename... TArguments>
	ROXELL_NOINLINE TValue& Array<TValue>::addWithGrowth(TArguments&&... arguments) noexcept
	{
		const usize newCapacity = growthCapacity(m_Size + 1);
		TValue* const newData = allocateRequired(newCapacity);
		core::ConstructAt(newData + m_Size, core::Forward<TArguments>(arguments)...);
		moveTo(newData);
		replaceStorage(newData, newCapacity);
		return m_Data[m_Size++];
	}

	template <typename TValue>
	void Array<TValue>::replaceStorage(TValue* newData, const usize newCapacity) noexcept
	{
		ASSERT(newData != nullptr, "Replacement Array storage must be valid.");
		ASSERT(newCapacity >= m_Size, "Replacement Array storage must fit every initialized element.");
		ASSERT(m_Resource != nullptr, "Array must retain its storage resource during replacement.");
		if constexpr (!core::Relocation<TValue>::IsCustomized)
		{
			destroyRange(0, m_Size);
		}

		if (m_Data != nullptr)
		{
			m_Resource->Deallocate(m_Data, {byteCount(m_Capacity), alignof(TValue)});
		}

		m_Data = newData;
		m_Capacity = newCapacity;
	}

	template <typename TValue>
	void Array<TValue>::copyFrom(const Array& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		if (other.m_Size == 0)
		{
			return;
		}

		m_Data = allocateRequired(other.m_Size);
		m_Capacity = other.m_Size;
		if constexpr (IsTriviallyCopyable<TValue>)
		{
			core::CopyBytes(m_Data, other.m_Data, other.m_Size * sizeof(TValue));
			m_Size = other.m_Size;
		}
		else
		{
			for (const TValue& value : other)
			{
				core::ConstructAt(m_Data + m_Size, value);
				++m_Size;
			}
		}
	}

	template <typename TValue>
	void Array<TValue>::destroyRange(const usize first, const usize last) noexcept
	{
		ASSERT(first <= last && last <= m_Size, "Array destruction range must be within initialized elements.");
		ASSERT(m_Data != nullptr || first == last, "A non-empty Array destruction range requires valid storage.");
		if constexpr (!IsTriviallyDestructible<TValue>)
		{
			for (usize index = first; index < last; ++index)
			{
				core::DestroyAt(m_Data + index);
			}
		}
	}

	template <typename TValue>
	void Array<TValue>::releaseStorage() noexcept
	{
		ASSERT(m_Resource != nullptr, "Array must retain its resource until storage is released.");
		destroyRange(0, m_Size);

		if (m_Data != nullptr)
		{
			m_Resource->Deallocate(m_Data, {byteCount(m_Capacity), alignof(TValue)});
		}

		m_Data = nullptr;
		m_Size = 0;
		m_Capacity = 0;
	}

	template <typename TValue>
	void Array<TValue>::stealStorage(Array& source) noexcept
	{
		ASSERT(m_Resource == source.m_Resource,
		       "Array storage can be stolen only between arrays using the same memory resource.");
		m_Data = source.m_Data;
		m_Size = source.m_Size;
		m_Capacity = source.m_Capacity;
		source.m_Data = nullptr;
		source.m_Size = 0;
		source.m_Capacity = 0;
	}
}
