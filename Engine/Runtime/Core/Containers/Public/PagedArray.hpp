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
	template <typename T, rox::core::usize PageSize = 64> 
	class PagedArray final
	{
		static_assert(rox::core::IsObject<T> && !rox::core::IsArray<T> && rox::core::IsSame<T, rox::core::RemoveCv<T>>);
		static_assert(rox::core::IsNothrowDestructible<T>);
		static_assert(PageSize > 0 && PageSize <= rox::core::NumericLimits<rox::core::usize>::max() / sizeof(T));
		static constexpr rox::core::usize PAGE_BYTES = sizeof(T) * PageSize;

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
		    requires rox::core::IsNothrowCopyConstructible<T>;

		PagedArray& operator=(const PagedArray& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

		PagedArray(PagedArray&& other) noexcept;

		PagedArray& operator=(PagedArray&& other) noexcept;

		[[nodiscard]] rox::core::usize Size() const noexcept;
		[[nodiscard]] rox::core::usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		// Failure preserves live values, their addresses and page capacity; table storage may grow.
		[[nodiscard]] bool Reserve(rox::core::usize capacity) noexcept;
		void ReserveRequired(rox::core::usize capacity) noexcept;

		template <typename... Arguments>
		[[nodiscard]] T* Emplace(Arguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<T, Arguments&&...>;

		template <typename... Arguments>
		T& EmplaceRequired(Arguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<T, Arguments&&...>;

		[[nodiscard]] T& At(rox::core::usize index) noexcept;
		[[nodiscard]] const T& At(rox::core::usize index) const noexcept;

		[[nodiscard]] T& operator[](rox::core::usize index) noexcept;
		[[nodiscard]] const T& operator[](rox::core::usize index) const noexcept;

		T& AddRequired(const T& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

		T& AddRequired(T&& value) noexcept
		    requires rox::core::IsNothrowMoveConstructible<T>;

		[[nodiscard]] bool Add(const T& value) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

		[[nodiscard]] bool Add(T&& value) noexcept
		    requires rox::core::IsNothrowMoveConstructible<T>;

		[[nodiscard]] T& Front() noexcept;
		[[nodiscard]] const T& Front() const noexcept;

		[[nodiscard]] T& Back() noexcept;
		[[nodiscard]] const T& Back() const noexcept;

		void PopBack() noexcept;

		void Clear() noexcept;
		void Reset() noexcept;

		[[nodiscard]] bool CloneTo(MemoryResource& resource, PagedArray* outArray) const noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

	private:
		// The destination is empty, reserved and distinct from the source.
		void copyReserved(const PagedArray& other) noexcept
		    requires rox::core::IsNothrowCopyConstructible<T>;

		T* storage(rox::core::usize index) noexcept;
		const T* storage(rox::core::usize index) const noexcept;

	private:
		Array<Page> m_Pages;
		rox::core::usize m_Size = 0;
	};
}

namespace rox::core
{
	template <typename T, rox::core::usize PageSize>
	PagedArray<T, PageSize>::Page::Page(T* data, MemoryResource& owner) noexcept : Data(data), Owner(&owner)
	{
	}

	template <typename T, rox::core::usize PageSize>
	PagedArray<T, PageSize>::Page::Page(Page&& other) noexcept
	    : Data(rox::core::Exchange(other.Data, nullptr)), Owner(other.Owner)
	{
	}

	template <typename T, rox::core::usize PageSize>
	PagedArray<T, PageSize>::Page::~Page() noexcept
	{
		T* const data = rox::core::Exchange(Data, nullptr);
		if (data != nullptr)
		{
			Owner->Deallocate(data, {PAGE_BYTES, alignof(T)});
		}
	}

	template <typename T, rox::core::usize PageSize>
	PagedArray<T, PageSize>::PagedArray(MemoryResource& resource) noexcept
	    : m_Pages(resource)
	{
	}

	template <typename T, rox::core::usize PageSize>
	PagedArray<T, PageSize>::~PagedArray() noexcept
	{
		Clear();
	}

	template <typename T, rox::core::usize PageSize>
	PagedArray<T, PageSize>::PagedArray(const PagedArray& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	    : PagedArray(other.GetResource())
	{
		ReserveRequired(other.Size());

		copyReserved(other);
	}

	template <typename T, rox::core::usize PageSize>
	PagedArray<T, PageSize>& PagedArray<T, PageSize>::operator=(const PagedArray& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
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

	template <typename T, rox::core::usize PageSize>
	PagedArray<T, PageSize>::PagedArray(PagedArray&& other) noexcept
	    : m_Pages(rox::core::Move(other.m_Pages))
	    , m_Size(rox::core::Exchange(other.m_Size, 0))
	{
	}

	template <typename T, rox::core::usize PageSize>
	PagedArray<T, PageSize>& PagedArray<T, PageSize>::operator=(PagedArray&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}
		Clear();
		m_Pages = rox::core::Move(other.m_Pages);
		m_Size = rox::core::Exchange(other.m_Size, 0);
		return *this;
	}

	template <typename T, rox::core::usize PageSize>
	rox::core::usize PagedArray<T, PageSize>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename T, rox::core::usize PageSize>
	rox::core::usize PagedArray<T, PageSize>::Capacity() const noexcept
	{
		return m_Pages.Size() * PageSize;
	}

	template <typename T, rox::core::usize PageSize>
	bool PagedArray<T, PageSize>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename T, rox::core::usize PageSize>
	MemoryResource& PagedArray<T, PageSize>::GetResource() const noexcept
	{
		return m_Pages.GetResource();
	}

	template <typename T, rox::core::usize PageSize>
	bool PagedArray<T, PageSize>::Reserve(rox::core::usize capacity) noexcept
	{
		const rox::core::usize pages = capacity / PageSize + (capacity % PageSize != 0 ? 1 : 0);
		if (pages > rox::core::NumericLimits<rox::core::usize>::max() / PageSize || !m_Pages.Grow(pages))
		{
			return false;
		}

		const rox::core::usize previousPages = m_Pages.Size();
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

	template <typename T, rox::core::usize PageSize>
	void PagedArray<T, PageSize>::ReserveRequired(rox::core::usize capacity) noexcept
	{
		const rox::core::usize pages = capacity / PageSize + (capacity % PageSize != 0 ? 1 : 0);
		ASSERT(pages <= rox::core::NumericLimits<rox::core::usize>::max() / PageSize, "Paged array capacity must remain representable.");

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

	template <typename T, rox::core::usize PageSize>
	template <typename... Arguments>
	T* PagedArray<T, PageSize>::Emplace(Arguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<T, Arguments&&...>
	{
		if (m_Size == rox::core::NumericLimits<rox::core::usize>::max() || (m_Size == Capacity() && !Reserve(m_Size + 1)))
		{
			return nullptr;
		}
		T* value = rox::core::ConstructAt(storage(m_Size), rox::core::Forward<Arguments>(arguments)...);
		++m_Size;
		return value;
	}

	template <typename T, rox::core::usize PageSize>
	template <typename... Arguments>
	T& PagedArray<T, PageSize>::EmplaceRequired(Arguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<T, Arguments&&...>
	{
		ASSERT(m_Size < rox::core::NumericLimits<rox::core::usize>::max(), "Paged array capacity must remain representable.");
		if (m_Size == Capacity())
		{
			ReserveRequired(m_Size + 1);
		}
		T* value = rox::core::ConstructAt(storage(m_Size), rox::core::Forward<Arguments>(arguments)...);
		++m_Size;
		return *value;
	}

	template <typename T, rox::core::usize PageSize>
	T& PagedArray<T, PageSize>::At(rox::core::usize index) noexcept
	{
		ASSERT(index < m_Size, "Paged array index must identify a live element.");
		return *rox::core::Launder(storage(index));
	}

	template <typename T, rox::core::usize PageSize>
	const T& PagedArray<T, PageSize>::At(rox::core::usize index) const noexcept
	{
		ASSERT(index < m_Size, "Paged array index must identify a live element.");
		return *rox::core::Launder(storage(index));
	}

	template <typename T, rox::core::usize PageSize>
	T& PagedArray<T, PageSize>::operator[](rox::core::usize index) noexcept
	{
		return At(index);
	}

	template <typename T, rox::core::usize PageSize>
	const T& PagedArray<T, PageSize>::operator[](rox::core::usize index) const noexcept
	{
		return At(index);
	}

	template <typename T, rox::core::usize PageSize>
	T& PagedArray<T, PageSize>::AddRequired(const T& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	{
		return EmplaceRequired(value);
	}

	template <typename T, rox::core::usize PageSize>
	T& PagedArray<T, PageSize>::AddRequired(T&& value) noexcept
	    requires rox::core::IsNothrowMoveConstructible<T>
	{
		return EmplaceRequired(rox::core::Move(value));
	}

	template <typename T, rox::core::usize PageSize>
	bool PagedArray<T, PageSize>::Add(const T& value) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	{
		return Emplace(value) != nullptr;
	}

	template <typename T, rox::core::usize PageSize>
	bool PagedArray<T, PageSize>::Add(T&& value) noexcept
	    requires rox::core::IsNothrowMoveConstructible<T>
	{
		return Emplace(rox::core::Move(value)) != nullptr;
	}

	template <typename T, rox::core::usize PageSize>
	T& PagedArray<T, PageSize>::Front() noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		return At(0);
	}

	template <typename T, rox::core::usize PageSize>
	const T& PagedArray<T, PageSize>::Front() const noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		return At(0);
	}

	template <typename T, rox::core::usize PageSize>
	T& PagedArray<T, PageSize>::Back() noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		return At(m_Size - 1);
	}

	template <typename T, rox::core::usize PageSize>
	const T& PagedArray<T, PageSize>::Back() const noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		return At(m_Size - 1);
	}

	template <typename T, rox::core::usize PageSize>
	void PagedArray<T, PageSize>::PopBack() noexcept
	{
		ASSERT(m_Size != 0, "Paged array operation requires a non-empty array.");
		--m_Size;
		if constexpr (!rox::core::IsTriviallyDestructible<T>)
		{
			rox::core::DestroyAt(rox::core::Launder(storage(m_Size)));
		}
	}

	template <typename T, rox::core::usize PageSize>
	void PagedArray<T, PageSize>::Clear() noexcept
	{
		if constexpr (!rox::core::IsTriviallyDestructible<T>)
		{
			while (m_Size != 0)
			{
				--m_Size;
				rox::core::DestroyAt(rox::core::Launder(storage(m_Size)));
			}
		}
		else
		{
			m_Size = 0;
		}
	}

	template <typename T, rox::core::usize PageSize>
	void PagedArray<T, PageSize>::Reset() noexcept
	{
		Clear();
		m_Pages.Reset();
	}

	template <typename T, rox::core::usize PageSize>
	bool PagedArray<T, PageSize>::CloneTo(MemoryResource& resource, PagedArray* outArray) const noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	{
		ASSERT(outArray != nullptr && &outArray->GetResource() == &resource, "PagedArray::CloneTo output must use the requested resource.");
		
		PagedArray replacement(resource);
		if (!replacement.Reserve(m_Size))
		{
			return false;
		}
		replacement.copyReserved(*this);
		*outArray = rox::core::Move(replacement);
		return true;
	}

	template <typename T, rox::core::usize PageSize>
	void PagedArray<T, PageSize>::copyReserved(const PagedArray& other) noexcept
	    requires rox::core::IsNothrowCopyConstructible<T>
	{
		if constexpr (rox::core::IsTriviallyCopyable<T>)
		{
			for (rox::core::usize first = 0; first < other.m_Size;)
			{
				const rox::core::usize remaining = other.m_Size - first;
				const rox::core::usize count = remaining < PageSize ? remaining : PageSize;
				// memcpy starts the implicit-lifetime values in the destination's raw page storage.
				rox::core::CopyBytes(storage(first), other.storage(first), count * sizeof(T));
				first += count;
			}
			m_Size = other.m_Size;
		}
		else
		{
			for (rox::core::usize index = 0; index < other.m_Size; ++index)
			{
				EmplaceRequired(other[index]);
			}
		}
	}

	template <typename T, rox::core::usize PageSize>
	T* PagedArray<T, PageSize>::storage(rox::core::usize index) noexcept
	{
		auto* page = reinterpret_cast<rox::core::Byte*>(m_Pages[index / PageSize].Data);
		return reinterpret_cast<T*>(page + (index % PageSize) * sizeof(T));
	}

	template <typename T, rox::core::usize PageSize>
	const T* PagedArray<T, PageSize>::storage(rox::core::usize index) const noexcept
	{
		const auto* page = reinterpret_cast<const rox::core::Byte*>(m_Pages[index / PageSize].Data);
		return reinterpret_cast<const T*>(page + (index % PageSize) * sizeof(T));
	}
}
