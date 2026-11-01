#pragma once

#include <Array.hpp>
#include <ByteOperations.hpp>
#include <Types.h>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <MemoryResource.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// Page-table relocation never moves live values. Every page keeps the resource that allocated it.
	template <typename T, usize PageSize = 64>
	class PagedArray final
	{
		static_assert(IsObject<T> && !IsArray<T> && IsSame<T, RemoveCv<T>>);
		static_assert(IsNothrowDestructible<T>);
		static_assert(PageSize > 0 && PageSize <= NumericLimits<usize>::max() / sizeof(T));
		static constexpr usize PAGE_BYTES = sizeof(T) * PageSize;

		// Page dimensions belong to the type; only storage and its original owner vary.
		struct Page final
		{
			T* Data;
			MemoryResource* Owner;

			Page(T* data, MemoryResource& owner) noexcept;
			Page(const Page&) = delete;
			Page& operator=(const Page&) = delete;
			Page(Page&& other) noexcept;
			~Page() noexcept;
		};

	public:
		explicit PagedArray(MemoryResource& resource) noexcept;

		~PagedArray() noexcept;

		PagedArray(const PagedArray& other) noexcept
		    requires IsNothrowCopyConstructible<T>;

		PagedArray& operator=(const PagedArray& other) noexcept
		    requires IsNothrowCopyConstructible<T>;

		PagedArray(PagedArray&& other) noexcept;

		PagedArray& operator=(PagedArray&& other) noexcept;

		[[nodiscard]] usize Size() const noexcept;
		[[nodiscard]] usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		// Failure preserves live values, their addresses and page capacity; table storage may grow.
		[[nodiscard]] bool Reserve(usize capacity) noexcept;
		void ReserveRequired(usize capacity) noexcept;

		template <typename... Arguments>
		[[nodiscard]] T* Emplace(Arguments&&... arguments) noexcept
		    requires IsNothrowConstructible<T, Arguments&&...>;

		template <typename... Arguments>
		T& EmplaceRequired(Arguments&&... arguments) noexcept
		    requires IsNothrowConstructible<T, Arguments&&...>;

		[[nodiscard]] T& At(usize index) noexcept;
		[[nodiscard]] const T& At(usize index) const noexcept;

		[[nodiscard]] T& operator[](usize index) noexcept;
		[[nodiscard]] const T& operator[](usize index) const noexcept;

		T& AddRequired(const T& value) noexcept
		    requires IsNothrowCopyConstructible<T>;

		T& AddRequired(T&& value) noexcept
		    requires IsNothrowMoveConstructible<T>;

		[[nodiscard]] bool Add(const T& value) noexcept
		    requires IsNothrowCopyConstructible<T>;

		[[nodiscard]] bool Add(T&& value) noexcept
		    requires IsNothrowMoveConstructible<T>;

		[[nodiscard]] T& Front() noexcept;
		[[nodiscard]] const T& Front() const noexcept;

		[[nodiscard]] T& Back() noexcept;
		[[nodiscard]] const T& Back() const noexcept;

		void PopBack() noexcept;

		void Clear() noexcept;
		void Reset() noexcept;

		[[nodiscard]] bool CloneTo(MemoryResource& resource, PagedArray* outArray) const noexcept
		    requires IsNothrowCopyConstructible<T>;

	private:
		// The destination is empty, reserved and distinct from the source.
		void copyReserved(const PagedArray& other) noexcept
		    requires IsNothrowCopyConstructible<T>;

		T* storage(usize index) noexcept;
		const T* storage(usize index) const noexcept;

	private:
		Array<Page> m_Pages;
		usize m_Size = 0;
	};
}

namespace rox::core
{
	template <typename T, usize PageSize>
	PagedArray<T, PageSize>::Page::Page(T* data, MemoryResource& owner) noexcept : Data(data), Owner(&owner)
	{
	}

	template <typename T, usize PageSize>
	PagedArray<T, PageSize>::Page::Page(Page&& other) noexcept
	    : Data(core::Exchange(other.Data, nullptr)), Owner(other.Owner)
	{
	}

	template <typename T, usize PageSize>
	PagedArray<T, PageSize>::Page::~Page() noexcept
	{
		T* const data = core::Exchange(Data, nullptr);
		if (data != nullptr)
		{
			Owner->Deallocate(data, {PAGE_BYTES, alignof(T)});
		}
	}

	template <typename T, usize PageSize>
	PagedArray<T, PageSize>::PagedArray(MemoryResource& resource) noexcept
	    : m_Pages(resource)
	{
	}

	template <typename T, usize PageSize>
	PagedArray<T, PageSize>::~PagedArray() noexcept
	{
		Clear();
	}

	template <typename T, usize PageSize>
	PagedArray<T, PageSize>::PagedArray(const PagedArray& other) noexcept
	    requires IsNothrowCopyConstructible<T>
	    : PagedArray(other.GetResource())
	{
		ReserveRequired(other.Size());

		copyReserved(other);
	}

	template <typename T, usize PageSize>
	PagedArray<T, PageSize>& PagedArray<T, PageSize>::operator=(const PagedArray& other) noexcept
	    requires IsNothrowCopyConstructible<T>
	{
		if (this == &other)
		{
			return *this;
		}
		Clear();
		ReserveRequired(other.Size());

		copyReserved(other);

		return *this;
	}

	template <typename T, usize PageSize>
	PagedArray<T, PageSize>::PagedArray(PagedArray&& other) noexcept
	    : m_Pages(core::Move(other.m_Pages))
	    , m_Size(core::Exchange(other.m_Size, 0))
	{
	}

	template <typename T, usize PageSize>
	PagedArray<T, PageSize>& PagedArray<T, PageSize>::operator=(PagedArray&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}
		Clear();
		m_Pages = core::Move(other.m_Pages);
		m_Size = core::Exchange(other.m_Size, 0);
		return *this;
	}

	template <typename T, usize PageSize>
	usize PagedArray<T, PageSize>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename T, usize PageSize>
	usize PagedArray<T, PageSize>::Capacity() const noexcept
	{
		return m_Pages.Size() * PageSize;
	}

	template <typename T, usize PageSize>
	bool PagedArray<T, PageSize>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename T, usize PageSize>
	MemoryResource& PagedArray<T, PageSize>::GetResource() const noexcept
	{
		return m_Pages.GetResource();
	}

	template <typename T, usize PageSize>
	bool PagedArray<T, PageSize>::Reserve(usize capacity) noexcept
	{
		const usize pages = capacity / PageSize + (capacity % PageSize != 0 ? 1 : 0);
		if (pages > NumericLimits<usize>::max() / PageSize || !m_Pages.Grow(pages))
		{
			return false;
		}

		const usize previousPages = m_Pages.Size();
		while (m_Pages.Size() < pages)
		{
			T* const data = static_cast<T*>(GetResource().Allocate({PAGE_BYTES, alignof(T)}));
			if (data == nullptr)
			{
				while (m_Pages.Size() > previousPages)
				{
					m_Pages.PopBack();
				}
				return false;
			}
			m_Pages.EmplaceRequired(data, GetResource());
		}
		return true;
	}

	template <typename T, usize PageSize>
	void PagedArray<T, PageSize>::ReserveRequired(usize capacity) noexcept
	{
		const usize pages = capacity / PageSize + (capacity % PageSize != 0 ? 1 : 0);
		ASSERT(pages <= NumericLimits<usize>::max() / PageSize, "Paged array capacity must remain representable.");

		// Bulk reservation knows the final table size; single-page growth stays geometric.
		if (pages > m_Pages.Size() && pages - m_Pages.Size() > 1)
		{
			m_Pages.ReserveRequired(pages);
		}
		while (m_Pages.Size() < pages)
		{
			T* const data = static_cast<T*>(GetResource().AllocateRequired({PAGE_BYTES, alignof(T)}));
			m_Pages.EmplaceRequired(data, GetResource());
		}
	}

	template <typename T, usize PageSize>
	template <typename... Arguments>
	T* PagedArray<T, PageSize>::Emplace(Arguments&&... arguments) noexcept
	    requires IsNothrowConstructible<T, Arguments&&...>
	{
		if (m_Size == NumericLimits<usize>::max() || (m_Size == Capacity() && !Reserve(m_Size + 1)))
		{
			return nullptr;
		}
		T* value = core::ConstructAt(storage(m_Size), core::Forward<Arguments>(arguments)...);
		++m_Size;
		return value;
	}

	template <typename T, usize PageSize>
	template <typename... Arguments>
	T& PagedArray<T, PageSize>::EmplaceRequired(Arguments&&... arguments) noexcept
	    requires IsNothrowConstructible<T, Arguments&&...>
	{
		ASSERT(m_Size < NumericLimits<usize>::max(), "Paged array capacity must remain representable.");
		if (m_Size == Capacity())
		{
			ReserveRequired(m_Size + 1);
		}
		T* value = core::ConstructAt(storage(m_Size), core::Forward<Arguments>(arguments)...);
		++m_Size;
		return *value;
	}

	template <typename T, usize PageSize>
	T& PagedArray<T, PageSize>::At(usize index) noexcept
	{
		ASSERT(index < m_Size, "Paged array index must identify a live element.");
		return *core::Launder(storage(index));
	}

	template <typename T, usize PageSize>
	const T& PagedArray<T, PageSize>::At(usize index) const noexcept
	{
		ASSERT(index < m_Size, "Paged array index must identify a live element.");
		return *core::Launder(storage(index));
	}

	template <typename T, usize PageSize>
	T& PagedArray<T, PageSize>::operator[](usize index) noexcept
	{
		return At(index);
	}

	template <typename T, usize PageSize>
	const T& PagedArray<T, PageSize>::operator[](usize index) const noexcept
	{
		return At(index);
	}

	template <typename T, usize PageSize>
	T& PagedArray<T, PageSize>::AddRequired(const T& value) noexcept
	    requires IsNothrowCopyConstructible<T>
	{
		return EmplaceRequired(value);
	}

	template <typename T, usize PageSize>
	T& PagedArray<T, PageSize>::AddRequired(T&& value) noexcept
	    requires IsNothrowMoveConstructible<T>
	{
		return EmplaceRequired(core::Move(value));
	}

	template <typename T, usize PageSize>
	bool PagedArray<T, PageSize>::Add(const T& value) noexcept
	    requires IsNothrowCopyConstructible<T>
	{
		return Emplace(value) != nullptr;
	}

	template <typename T, usize PageSize>
	bool PagedArray<T, PageSize>::Add(T&& value) noexcept
	    requires IsNothrowMoveConstructible<T>
	{
		return Emplace(core::Move(value)) != nullptr;
	}

	template <typename T, usize PageSize>
	T& PagedArray<T, PageSize>::Front() noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		return At(0);
	}

	template <typename T, usize PageSize>
	const T& PagedArray<T, PageSize>::Front() const noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		return At(0);
	}

	template <typename T, usize PageSize>
	T& PagedArray<T, PageSize>::Back() noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		return At(m_Size - 1);
	}

	template <typename T, usize PageSize>
	const T& PagedArray<T, PageSize>::Back() const noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		return At(m_Size - 1);
	}

	template <typename T, usize PageSize>
	void PagedArray<T, PageSize>::PopBack() noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		--m_Size;
		if constexpr (!IsTriviallyDestructible<T>)
		{
			core::DestroyAt(core::Launder(storage(m_Size)));
		}
	}

	template <typename T, usize PageSize>
	void PagedArray<T, PageSize>::Clear() noexcept
	{
		if constexpr (!IsTriviallyDestructible<T>)
		{
			while (m_Size != 0)
			{
				--m_Size;
				core::DestroyAt(core::Launder(storage(m_Size)));
			}
		}
		else
		{
			m_Size = 0;
		}
	}

	template <typename T, usize PageSize>
	void PagedArray<T, PageSize>::Reset() noexcept
	{
		Clear();
		m_Pages.Reset();
	}

	template <typename T, usize PageSize>
	bool PagedArray<T, PageSize>::CloneTo(MemoryResource& resource, PagedArray* outArray) const noexcept
	    requires IsNothrowCopyConstructible<T>
	{
		ASSERT(outArray != nullptr && &outArray->GetResource() == &resource, "PagedArray::CloneTo output must use the requested resource.");
		
		PagedArray replacement(resource);
		if (!replacement.Reserve(m_Size))
		{
			return false;
		}
		replacement.copyReserved(*this);
		*outArray = core::Move(replacement);
		return true;
	}

	template <typename T, usize PageSize>
	void PagedArray<T, PageSize>::copyReserved(const PagedArray& other) noexcept
	    requires IsNothrowCopyConstructible<T>
	{
		if constexpr (IsTriviallyCopyable<T>)
		{
			for (usize first = 0; first < other.m_Size;)
			{
				const usize remaining = other.m_Size - first;
				const usize count = remaining < PageSize ? remaining : PageSize;
				// memcpy starts the implicit-lifetime values in the destination's raw page storage.
				core::CopyBytes(storage(first), other.storage(first), count * sizeof(T));
				first += count;
			}
			m_Size = other.m_Size;
		}
		else
		{
			for (usize index = 0; index < other.m_Size; ++index)
			{
				EmplaceRequired(other[index]);
			}
		}
	}

	template <typename T, usize PageSize>
	T* PagedArray<T, PageSize>::storage(usize index) noexcept
	{
		auto* page = reinterpret_cast<Byte*>(m_Pages[index / PageSize].Data);
		return reinterpret_cast<T*>(page + (index % PageSize) * sizeof(T));
	}

	template <typename T, usize PageSize>
	const T* PagedArray<T, PageSize>::storage(usize index) const noexcept
	{
		const auto* page = reinterpret_cast<const Byte*>(m_Pages[index / PageSize].Data);
		return reinterpret_cast<const T*>(page + (index % PageSize) * sizeof(T));
	}
}
