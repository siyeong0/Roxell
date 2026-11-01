#pragma once

#include <Assertion.h>
#include <CompilerAttributes.h>
#include <Algorithms.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <Span.hpp>

#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// Mutation requires exclusive access. Inline element addresses remain owned by this object.
	template <typename TValue, usize N>
	class InplaceVector final
	{
		static_assert(IsObject<TValue> && !IsArray<TValue>);
		static_assert(IsSame<TValue, RemoveCv<TValue>>);
		static_assert(IsNothrowMoveConstructible<TValue> && IsNothrowDestructible<TValue>);
		static_assert(N <= NumericLimits<usize>::max() / sizeof(TValue));

	public:
		[[nodiscard]] TValue& At(usize index) noexcept;
		[[nodiscard]] TValue& Front() noexcept;
		[[nodiscard]] TValue& Back() noexcept;

		[[nodiscard]] const TValue& At(usize index) const noexcept;
		[[nodiscard]] const TValue& Front() const noexcept;
		[[nodiscard]] const TValue& Back() const noexcept;

		InplaceVector() noexcept = default;
		~InplaceVector() noexcept;

		InplaceVector(const InplaceVector& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		InplaceVector(InplaceVector&& other) noexcept;

		// Assignment invalidates destination borrows; moving also invalidates source borrows.
		// Nontrivial copies are staged before releasing destination-owned objects, which may own the source.
		InplaceVector& operator=(const InplaceVector& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		// Except for self-assignment, the ownership graphs must be disjoint: clearing this vector must not
		// destroy the source or any object its copies require. Invalidates destination views.
		InplaceVector& AssignDisjoint(const InplaceVector& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		InplaceVector& operator=(InplaceVector&& other) noexcept;

		[[nodiscard]] TValue* data() noexcept;
		[[nodiscard]] const TValue* data() const noexcept;
		[[nodiscard]] usize size() const noexcept;
		[[nodiscard]] bool empty() const noexcept;

		[[nodiscard]] usize Size() const noexcept;
		[[nodiscard]] usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;
		[[nodiscard]] bool IsFull() const noexcept;

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

		[[nodiscard]] ROXELL_FORCE_INLINE TValue* Find(const TValue& value) noexcept
		    requires requires(const TValue& left, const TValue& right) 
			{
			    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
		    };

		[[nodiscard]] ROXELL_FORCE_INLINE const TValue* Find(const TValue& value) const noexcept
		    requires requires(const TValue& left, const TValue& right)
			{
			    { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;
		    };

		[[nodiscard]] ROXELL_FORCE_INLINE bool Contains(const TValue& value) const noexcept
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

		TValue& operator[](const usize index) noexcept;
		const TValue& operator[](const usize index) const noexcept;

		template <typename... TArguments>
		TValue& EmplaceRequired(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments...>;

		TValue& AddRequired(const TValue& value) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		TValue& AddRequired(TValue&& value) noexcept;

		template <typename... TArguments>
		[[nodiscard]] TValue* Emplace(TArguments&&... arguments) noexcept
		    requires IsNothrowConstructible<TValue, TArguments...>;

		[[nodiscard]] bool Add(const TValue& value) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		[[nodiscard]] bool Add(TValue&& value) noexcept;

		// Source may borrow any initialized self-subrange; existing element addresses remain valid.
		void AppendRequired(const Span<const TValue> source) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		[[nodiscard]] bool Append(const Span<const TValue> source) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		[[nodiscard]] bool Resize(const usize size) noexcept
		    requires IsNothrowDefaultConstructible<TValue>;

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

	private:
		template <typename... TArguments>
		TValue& insertValue(const usize index, TArguments&&... arguments) noexcept;

		TValue* inlineData() noexcept;
		const TValue* inlineData() const noexcept;

		void destroyRange(const usize first, const usize last) noexcept;

		// Keep the temporary's full inline storage off the empty-destination assignment stack.
		ROXELL_NOINLINE void assignWithStaging(const InplaceVector& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		void copyFrom(const InplaceVector& other) noexcept
		    requires IsNothrowCopyConstructible<TValue>;

		void moveFrom(InplaceVector& other) noexcept;

	private:
		alignas(TValue) Byte m_Inline[N == 0 ? 1 : N * sizeof(TValue)];
		usize m_Size = 0;
	};
}

namespace rox::core
{
	template <typename TValue, usize N>
	TValue& InplaceVector<TValue, N>::At(usize index) noexcept
	{
		return (*this)[index];
	}

	template <typename TValue, usize N>
	TValue& InplaceVector<TValue, N>::Front() noexcept
	{
		ASSERT(m_Size != 0, "InplaceVector::Front requires a non-empty range.");
		return (*this)[0];
	}

	template <typename TValue, usize N>
	TValue& InplaceVector<TValue, N>::Back() noexcept
	{
		ASSERT(m_Size != 0, "InplaceVector::Back requires a non-empty range.");
		return (*this)[m_Size - 1];
	}

	template <typename TValue, usize N>
	const TValue& InplaceVector<TValue, N>::At(usize index) const noexcept
	{
		return (*this)[index];
	}

	template <typename TValue, usize N>
	const TValue& InplaceVector<TValue, N>::Front() const noexcept
	{
		ASSERT(m_Size != 0, "InplaceVector::Front requires a non-empty range.");
		return (*this)[0];
	}

	template <typename TValue, usize N>
	const TValue& InplaceVector<TValue, N>::Back() const noexcept
	{
		ASSERT(m_Size != 0, "InplaceVector::Back requires a non-empty range.");
		return (*this)[m_Size - 1];
	}

	template <typename TValue, usize N>
	InplaceVector<TValue, N>::~InplaceVector() noexcept
	{
		Clear();
	}

	template <typename TValue, usize N>
	InplaceVector<TValue, N>::InplaceVector(const InplaceVector& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		copyFrom(other);
	}

	template <typename TValue, usize N>
	InplaceVector<TValue, N>::InplaceVector(InplaceVector&& other) noexcept
	{
		moveFrom(other);
	}

	template <typename TValue, usize N>
	InplaceVector<TValue, N>& InplaceVector<TValue, N>::operator=(const InplaceVector& other) noexcept
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
		// An empty destination has no element-owned source to invalidate.
		if (m_Size == 0)
		{
			copyFrom(other);
			return *this;
		}
		if constexpr (IsTriviallyCopyable<TValue>)
		{
			Clear();
			copyFrom(other);
		}
		else
		{
			assignWithStaging(other);
		}

		return *this;
	}

	template <typename TValue, usize N>
	InplaceVector<TValue, N>& InplaceVector<TValue, N>::AssignDisjoint(const InplaceVector& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		if (this != &other)
		{
			Clear();
			copyFrom(other);
		}
		return *this;
	}

	template <typename TValue, usize N>
	InplaceVector<TValue, N>& InplaceVector<TValue, N>::operator=(InplaceVector&& other) noexcept
	{
		if (this != &other)
		{
			Clear();
			moveFrom(other);
		}

		return *this;
	}

	template <typename TValue, usize N>
	TValue* InplaceVector<TValue, N>::data() noexcept
	{
		return Data();
	}

	template <typename TValue, usize N>
	const TValue* InplaceVector<TValue, N>::data() const noexcept
	{
		return Data();
	}

	template <typename TValue, usize N>
	usize InplaceVector<TValue, N>::size() const noexcept
	{
		return Size();
	}

	template <typename TValue, usize N>
	bool InplaceVector<TValue, N>::empty() const noexcept
	{
		return IsEmpty();
	}

	template <typename TValue, usize N>
	usize InplaceVector<TValue, N>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename TValue, usize N>
	usize InplaceVector<TValue, N>::Capacity() const noexcept
	{
		return N;
	}

	template <typename TValue, usize N>
	bool InplaceVector<TValue, N>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename TValue, usize N>
	bool InplaceVector<TValue, N>::IsFull() const noexcept
	{
		return m_Size == N;
	}

	template <typename TValue, usize N>
	TValue* InplaceVector<TValue, N>::Data() noexcept
	{
		return inlineData();
	}

	template <typename TValue, usize N>
	const TValue* InplaceVector<TValue, N>::Data() const noexcept
	{
		return inlineData();
	}

	template <typename TValue, usize N>
	Span<TValue> InplaceVector<TValue, N>::View() noexcept
	{
		return {Data(), m_Size};
	}

	template <typename TValue, usize N>
	Span<const TValue> InplaceVector<TValue, N>::View() const noexcept
	{
		return {Data(), m_Size};
	}

	template <typename TValue, usize N>
	template <typename TPredicate>
	ROXELL_FORCE_INLINE TValue* InplaceVector<TValue, N>::FindIf(TPredicate&& predicate) noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		if (m_Size == 0)
		{
			return nullptr;
		}
		TValue* cursor = Data();
		TValue* const blockEnd = cursor + (m_Size - m_Size % 8);
		TValue* const end = cursor + m_Size;
		while (cursor != blockEnd)
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
		while (cursor != end)
		{
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
		}
		return nullptr;
	}

	template <typename TValue, usize N>
	template <typename TPredicate>
	ROXELL_FORCE_INLINE const TValue* InplaceVector<TValue, N>::FindIf(TPredicate&& predicate) const noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		if (m_Size == 0)
		{
			return nullptr;
		}
		const TValue* cursor = Data();
		const TValue* const blockEnd = cursor + (m_Size - m_Size % 8);
		const TValue* const end = cursor + m_Size;
		while (cursor != blockEnd)
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
		while (cursor != end)
		{
			if (core::Invoke(predicate, core::AsConst(*cursor)))
			{
				return cursor;
			}
			++cursor;
		}
		return nullptr;
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE TValue* InplaceVector<TValue, N>::Find(const TValue& value) noexcept
	    requires requires(const TValue& left, const TValue& right) { { static_cast<bool>(left == right) } noexcept -> SameAs<bool>; }
	{
		if constexpr (IsIntegral<TValue>)
		{
			if (m_Size == 0)
			{
				return nullptr;
			}
			auto* const found = core::Find(begin(), end(), value);
			return found == end() ? nullptr : found;
		}
		return FindIf(
		    [&value](const TValue& candidate) noexcept
		    {
			    return static_cast<bool>(candidate == value);
		    });
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE const TValue* InplaceVector<TValue, N>::Find(const TValue& value) const noexcept
	    requires requires(const TValue& left, const TValue& right) { { static_cast<bool>(left == right) } noexcept -> SameAs<bool>;}
	{
		if constexpr (IsIntegral<TValue>)
		{
			if (m_Size == 0)
			{
				return nullptr;
			}
			auto* const found = core::Find(begin(), end(), value);
			return found == end() ? nullptr : found;
		}
		return FindIf(
		    [&value](const TValue& candidate) noexcept
		    {
			    return static_cast<bool>(candidate == value);
		    });
	}

	template <typename TValue, usize N>
	ROXELL_FORCE_INLINE bool InplaceVector<TValue, N>::Contains(const TValue& value) const noexcept
	    requires requires(const TValue& left, const TValue& right) { { static_cast<bool>(left == right) } noexcept -> SameAs<bool>; }
	{
		return Find(value) != nullptr;
	}

	template <typename TValue, usize N>
	template <typename TPredicate>
	bool InplaceVector<TValue, N>::ContainsIf(TPredicate&& predicate) const noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		return FindIf(core::Forward<TPredicate>(predicate)) != nullptr;
	}

	template <typename TValue, usize N>
	TValue* InplaceVector<TValue, N>::begin() noexcept
	{
		return Data();
	}

	template <typename TValue, usize N>
	const TValue* InplaceVector<TValue, N>::begin() const noexcept
	{
		return Data();
	}

	template <typename TValue, usize N>
	TValue* InplaceVector<TValue, N>::end() noexcept
	{
		return m_Size == 0 ? Data() : Data() + m_Size;
	}

	template <typename TValue, usize N>
	const TValue* InplaceVector<TValue, N>::end() const noexcept
	{
		return m_Size == 0 ? Data() : Data() + m_Size;
	}

	template <typename TValue, usize N>
	TValue& InplaceVector<TValue, N>::operator[](const usize index) noexcept
	{
		ASSERT(index < m_Size, "InplaceVector index must address a live element.");
		return Data()[index];
	}

	template <typename TValue, usize N>
	const TValue& InplaceVector<TValue, N>::operator[](const usize index) const noexcept
	{
		ASSERT(index < m_Size, "InplaceVector index must address a live element.");
		return Data()[index];
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue& InplaceVector<TValue, N>::EmplaceRequired(TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments...>
	{
		ASSERT(m_Size < N, "InplaceVector::EmplaceRequired requires available capacity.");
		TValue* const value = core::ConstructAt(Data() + m_Size, core::Forward<TArguments>(arguments)...);
		++m_Size;
		return *value;
	}

	template <typename TValue, usize N>
	TValue& InplaceVector<TValue, N>::AddRequired(const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return EmplaceRequired(value);
	}

	template <typename TValue, usize N>
	TValue& InplaceVector<TValue, N>::AddRequired(TValue&& value) noexcept
	{
		return EmplaceRequired(core::Move(value));
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue* InplaceVector<TValue, N>::Emplace(TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments...>
	{
		if (m_Size < N)
		{
			TValue* const value = core::ConstructAt(Data() + m_Size, core::Forward<TArguments>(arguments)...);
			++m_Size;
			return value;
		}

		ASSERT(m_Size == N, "InplaceVector::Emplace size must not exceed capacity.");
		return nullptr;
	}

	template <typename TValue, usize N>
	bool InplaceVector<TValue, N>::Add(const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return Emplace(value) != nullptr;
	}

	template <typename TValue, usize N>
	bool InplaceVector<TValue, N>::Add(TValue&& value) noexcept
	{
		return Emplace(core::Move(value)) != nullptr;
	}

	template <typename TValue, usize N>
	void InplaceVector<TValue, N>::AppendRequired(const Span<const TValue> source) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		ASSERT(source.Size() <= N - m_Size, "InplaceVector::AppendRequired source must fit remaining capacity.");

		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (!source.IsEmpty())
			{
				core::CopyBytes(Data() + m_Size, source.Data(), source.Size() * sizeof(TValue));
				m_Size += source.Size();
			}
		}
		else if constexpr (N > 0)
		{
			TValue* const data = Data();
			for (const TValue& value : source)
			{
				core::ConstructAt(data + m_Size, value);
				++m_Size;
			}
		}
	}

	template <typename TValue, usize N>
	bool InplaceVector<TValue, N>::Append(const Span<const TValue> source) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		if (source.Size() > N - m_Size)
		{
			return false;
		}
		AppendRequired(source);
		return true;
	}

	template <typename TValue, usize N>
	bool InplaceVector<TValue, N>::Resize(const usize size) noexcept
	    requires IsNothrowDefaultConstructible<TValue>
	{
		if (size > N)
		{
			return false;
		}
		ResizeRequired(size);
		return true;
	}

	template <typename TValue, usize N>
	void InplaceVector<TValue, N>::ResizeRequired(const usize size) noexcept
	    requires IsNothrowDefaultConstructible<TValue>
	{
		ASSERT(size <= N, "InplaceVector::ResizeRequired size must fit capacity.");

		if (size < m_Size)
		{
			destroyRange(size, m_Size);
			m_Size = size;
			return;
		}

		if constexpr (IsTrivial<TValue>)
		{
			if (size != m_Size)
			{
				core::ValueConstructN(Data() + m_Size, size - m_Size);
			}
			m_Size = size;
		}
		else
		{
			while (m_Size < size)
			{
				EmplaceRequired();
			}
		}
	}

	template <typename TValue, usize N>
	template <typename TPredicate>
	usize InplaceVector<TValue, N>::RemoveIf(TPredicate&& predicate) noexcept
	    requires IsNothrowInvocableR<bool, TPredicate&, const TValue&>
	{
		usize destination = 0;

		for (usize sourceIndex = 0; sourceIndex < m_Size; ++sourceIndex)
		{
			if (core::Invoke(predicate, core::AsConst(Data()[sourceIndex])))
			{
				core::DestroyAt(Data() + sourceIndex);
			}
			else
			{
				if (destination != sourceIndex)
				{
					core::detail::RelocateAtUnchecked(Data() + destination, Data() + sourceIndex);
				}
				++destination;
			}
		}

		const usize removed = m_Size - destination;
		m_Size = destination;
		return removed;
	}

	template <typename TValue, usize N>
	void InplaceVector<TValue, N>::RemoveAt(const usize index) noexcept
	{
		ASSERT(index < m_Size, "InplaceVector::RemoveAt index must identify an initialized element.");
		core::DestroyAt(Data() + index);

		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (index + 1 < m_Size)
			{
				core::MoveBytes(Data() + index, Data() + index + 1,
				                     (m_Size - index - 1) * sizeof(TValue));
				core::DestroyAt(Data() + m_Size - 1);
			}
		}
		else
		{
			for (usize sourceIndex = index + 1; sourceIndex < m_Size; ++sourceIndex)
			{
				core::detail::RelocateAtUnchecked(Data() + sourceIndex - 1, Data() + sourceIndex);
			}
		}
		--m_Size;
	}

	template <typename TValue, usize N>
	void InplaceVector<TValue, N>::RemoveAtSwap(const usize index) noexcept
	{
		ASSERT(index < m_Size, "InplaceVector::RemoveAtSwap index must identify an initialized element.");
		const usize lastIndex = m_Size - 1;
		core::DestroyAt(Data() + index);

		if (index != lastIndex)
		{
			core::detail::RelocateAtUnchecked(Data() + index, Data() + lastIndex);
		}
		--m_Size;
	}

	template <typename TValue, usize N>
	TValue& InplaceVector<TValue, N>::InsertRequired(const usize index, const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return insertValue(index, value);
	}

	template <typename TValue, usize N>
	TValue& InplaceVector<TValue, N>::InsertRequired(const usize index, TValue&& value) noexcept
	{
		return insertValue(index, core::Move(value));
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue& InplaceVector<TValue, N>::EmplaceAtRequired(const usize index, TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments&&...>
	{
		return insertValue(index, core::Forward<TArguments>(arguments)...);
	}

	template <typename TValue, usize N>
	TValue* InplaceVector<TValue, N>::Insert(const usize index, const TValue& value) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		return EmplaceAt(index, value);
	}

	template <typename TValue, usize N>
	TValue* InplaceVector<TValue, N>::Insert(const usize index, TValue&& value) noexcept
	{
		return EmplaceAt(index, core::Move(value));
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue* InplaceVector<TValue, N>::EmplaceAt(const usize index, TArguments&&... arguments) noexcept
	    requires IsNothrowConstructible<TValue, TArguments...>
	{
		ASSERT(index <= m_Size, "InplaceVector::EmplaceAt index must fit the initialized range including its end.");
		if (IsFull())
		{
			return nullptr;
		}
		return core::AddressOf(insertValue(index, core::Forward<TArguments>(arguments)...));
	}

	template <typename TValue, usize N>
	void InplaceVector<TValue, N>::PopBack() noexcept
	{
		ASSERT(m_Size != 0, "InplaceVector::PopBack requires a live element.");
		core::DestroyAt(Data() + m_Size - 1);
		--m_Size;
	}

	template <typename TValue, usize N>
	void InplaceVector<TValue, N>::Clear() noexcept
	{
		destroyRange(0, m_Size);
		m_Size = 0;
	}

	template <typename TValue, usize N>
	template <typename... TArguments>
	TValue& InplaceVector<TValue, N>::insertValue(const usize index, TArguments&&... arguments) noexcept
	{
		ASSERT(index <= m_Size, "InplaceVector::InsertRequired index must fit the initialized range including its end.");
		ASSERT(m_Size < N, "InplaceVector::InsertRequired requires available capacity.");
		TValue pending(core::Forward<TArguments>(arguments)...);

		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (index != m_Size)
			{
				core::MoveBytes(Data() + index + 1, Data() + index,
				                     (m_Size - index) * sizeof(TValue));
			}
		}
		else
		{
			for (usize destination = m_Size; destination > index; --destination)
			{
				core::detail::RelocateAtUnchecked(Data() + destination, Data() + destination - 1);
			}
		}

		core::ConstructAt(Data() + index, core::Move(pending));
		++m_Size;
		return Data()[index];
	}

	template <typename TValue, usize N>
	TValue* InplaceVector<TValue, N>::inlineData() noexcept
	{
		return N == 0 ? nullptr : reinterpret_cast<TValue*>(m_Inline);
	}

	template <typename TValue, usize N>
	const TValue* InplaceVector<TValue, N>::inlineData() const noexcept
	{
		return N == 0 ? nullptr : reinterpret_cast<const TValue*>(m_Inline);
	}

	template <typename TValue, usize N>
	void InplaceVector<TValue, N>::destroyRange(const usize first, const usize last) noexcept
	{
		if constexpr (!IsTriviallyDestructible<TValue>)
		{
			for (usize index = first; index < last; ++index)
			{
				core::DestroyAt(Data() + index);
			}
		}
	}

	template <typename TValue, usize N>
	ROXELL_NOINLINE void InplaceVector<TValue, N>::assignWithStaging(const InplaceVector& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		InplaceVector replacement(other);
		Clear();
		moveFrom(replacement);
	}

	template <typename TValue, usize N>
	void InplaceVector<TValue, N>::copyFrom(const InplaceVector& other) noexcept
	    requires IsNothrowCopyConstructible<TValue>
	{
		ASSERT(m_Size == 0 && other.m_Size <= N, "InplaceVector copy requires an empty destination and a source that fits capacity.");
		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (other.m_Size != 0)
			{
				core::CopyBytes(Data(), other.Data(), other.m_Size * sizeof(TValue));
			}
			m_Size = other.m_Size;
		}
		else
		{
			TValue* const data = Data();
			for (const TValue& value : other)
			{
				core::ConstructAt(data + m_Size, value);
				++m_Size;
			}
		}
	}

	template <typename TValue, usize N>
	void InplaceVector<TValue, N>::moveFrom(InplaceVector& other) noexcept
	{
		ASSERT(m_Size == 0 && other.m_Size <= N, "InplaceVector move requires an empty destination and a source that fits capacity.");
		if constexpr (IsTriviallyCopyable<TValue>)
		{
			if (other.m_Size != 0)
			{
				core::CopyBytes(Data(), other.Data(), other.m_Size * sizeof(TValue));
			}
			m_Size = other.m_Size;
		}
		else
		{
			TValue* const data = Data();
			for (TValue& value : other)
			{
				core::ConstructAt(data + m_Size, core::Move(value));
				++m_Size;
			}
		}
		other.Clear();
	}
}
