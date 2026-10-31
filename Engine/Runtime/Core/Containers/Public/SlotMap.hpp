#pragma once

#include <PagedArray.hpp>
#include <CompilerAttributes.h>
#include <Synchronization.hpp>
#include <Types.h>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <atomic>
#include <cstdint>
#include <functional>

namespace rox::core
{
	template <typename T, rox::core::usize PageSize> class SlotMap;

	template <typename T> 
	class SlotMapHandle final
	{
		template <typename, rox::core::usize> friend class SlotMap;

	public:
		SlotMapHandle() noexcept = default;

		[[nodiscard]] bool IsValid() const noexcept;

		[[nodiscard]] rox::core::usize GetIndex() const noexcept;
		[[nodiscard]] rox::core::uint64 GetGeneration() const noexcept;

		bool operator==(const SlotMapHandle&) const noexcept = default;

	private:
		SlotMapHandle(rox::core::uint64 owner, rox::core::usize index, rox::core::uint64 generation) noexcept;

		static rox::core::uint64 acquireOwner() noexcept;

	private:
		static inline rox::core::Atomic<rox::core::uint64> s_NextOwner{1};
		rox::core::uint64 m_Owner = 0;
		rox::core::usize m_Index = 0;
		rox::core::uint64 m_Generation = 0;
	};

	template <typename T, rox::core::usize PageSize = 64> class SlotMap final
	{
		static_assert(rox::core::IsObject<T> && !rox::core::IsArray<T> && rox::core::IsSame<T, rox::core::RemoveCv<T>>);
		static_assert(rox::core::IsNothrowDestructible<T>);
		static constexpr rox::core::usize NO_SLOT = rox::core::NumericLimits<rox::core::usize>::max();

		struct Slot final
		{
			alignas(T) rox::core::Byte Storage[sizeof(T)];
			rox::core::uint64 Generation = 1;
			rox::core::usize NextFree = NO_SLOT;
			bool Occupied = false;

			Slot() noexcept = default;
			Slot(const Slot&) = delete;
			Slot(Slot&&) = delete;
			~Slot() noexcept;

			T* value() noexcept;
			const T* value() const noexcept;
		};

	public:
		using Handle = SlotMapHandle<T>;

		explicit SlotMap(MemoryResource& resource) noexcept;
		SlotMap(const SlotMap&) = delete;
		SlotMap& operator=(const SlotMap&) = delete;
		SlotMap(SlotMap&& other) noexcept;
		SlotMap& operator=(SlotMap&& other) noexcept;

		[[nodiscard]] rox::core::usize Size() const noexcept;
		[[nodiscard]] rox::core::usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		void ReserveRequired(rox::core::usize capacity) noexcept;
		[[nodiscard]] bool Reserve(rox::core::usize capacity) noexcept;

		// Exhaustion returns an invalid handle without consuming arguments or changing existing handles.
		template <typename... Arguments>
		[[nodiscard]] Handle Emplace(Arguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<T, Arguments&&...>;

		template <typename... Arguments>
		Handle EmplaceRequired(Arguments&&... arguments) noexcept
		    requires rox::core::IsNothrowConstructible<T, Arguments&&...>;

		[[nodiscard]] ROXELL_FORCE_INLINE T* Find(Handle handle) noexcept;
		[[nodiscard]] ROXELL_FORCE_INLINE const T* Find(Handle handle) const noexcept;

		[[nodiscard]] bool Contains(Handle handle) const noexcept;

		[[nodiscard]] T& Get(Handle handle) noexcept;
		[[nodiscard]] const T& Get(Handle handle) const noexcept;

		bool Erase(Handle handle) noexcept;

		void Clear() noexcept;
		void Reset() noexcept;

		// Callbacks may update values but must not structurally mutate the map.
		template <typename Callback>
		void ForEach(Callback&& callback) noexcept
		    requires rox::core::IsNothrowInvocable<Callback&, Handle, T&>;

		template <typename Callback>
		void ForEach(Callback&& callback) const noexcept
		    requires rox::core::IsNothrowInvocable<Callback&, Handle, const T&>;

	private:
		[[nodiscard]] ROXELL_FORCE_INLINE const Slot* findSlot(Handle handle) const noexcept;
		[[nodiscard]] ROXELL_FORCE_INLINE Slot* findSlot(Handle handle) noexcept;

		void eraseSlot(Slot& slot, rox::core::usize index) noexcept;

	private:
		PagedArray<Slot, PageSize> m_Slots;
		rox::core::uint64 m_Owner;
		rox::core::usize m_Size = 0;
		rox::core::usize m_FreeHead = NO_SLOT;
	};
}

namespace rox::core
{
	template <typename T>
	bool SlotMapHandle<T>::IsValid() const noexcept
	{
		return m_Owner != 0 && m_Generation != 0;
	}

	template <typename T>
	rox::core::usize SlotMapHandle<T>::GetIndex() const noexcept
	{
		return m_Index;
	}

	template <typename T>
	rox::core::uint64 SlotMapHandle<T>::GetGeneration() const noexcept
	{
		return m_Generation;
	}

	template <typename T>
	SlotMapHandle<T>::SlotMapHandle(rox::core::uint64 owner, rox::core::usize index, rox::core::uint64 generation) noexcept
	    : m_Owner(owner)
	    , m_Index(index)
	    , m_Generation(generation)
	{
	}

	template <typename T>
	rox::core::uint64 SlotMapHandle<T>::acquireOwner() noexcept
	{
		const auto owner = s_NextOwner.fetch_add(1, rox::core::MemoryOrderRelaxed);
		ASSERT(owner != 0 && owner != rox::core::NumericLimits<rox::core::uint64>::max(),
		       "Slot map owner identity must remain representable.");
		return owner;
	}

	template <typename T, rox::core::usize PageSize>
	SlotMap<T, PageSize>::Slot::~Slot() noexcept
	{
		if (Occupied)
		{
			rox::core::DestroyAt(value());
		}
	}

	template <typename T, rox::core::usize PageSize>
	T* SlotMap<T, PageSize>::Slot::value() noexcept
	{
		return rox::core::Launder(reinterpret_cast<T*>(Storage));
	}

	template <typename T, rox::core::usize PageSize>
	const T* SlotMap<T, PageSize>::Slot::value() const noexcept
	{
		return rox::core::Launder(reinterpret_cast<const T*>(Storage));
	}

	template <typename T, rox::core::usize PageSize>
	SlotMap<T, PageSize>::SlotMap(MemoryResource& resource) noexcept
	    : m_Slots(resource)
	    , m_Owner(Handle::acquireOwner())
	{
	}

	template <typename T, rox::core::usize PageSize>
	SlotMap<T, PageSize>::SlotMap(SlotMap&& other) noexcept
	    : m_Slots(rox::core::Move(other.m_Slots))
	    , m_Owner(rox::core::Exchange(other.m_Owner, Handle::acquireOwner()))
	    , m_Size(rox::core::Exchange(other.m_Size, 0))
	    , m_FreeHead(rox::core::Exchange(other.m_FreeHead, NO_SLOT))
	{
	}

	template <typename T, rox::core::usize PageSize>
	SlotMap<T, PageSize>& SlotMap<T, PageSize>::operator=(SlotMap&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		m_Slots = rox::core::Move(other.m_Slots);
		m_Owner = rox::core::Exchange(other.m_Owner, Handle::acquireOwner());
		m_Size = rox::core::Exchange(other.m_Size, 0);
		m_FreeHead = rox::core::Exchange(other.m_FreeHead, NO_SLOT);
		return *this;
	}

	template <typename T, rox::core::usize PageSize>
	rox::core::usize SlotMap<T, PageSize>::Size() const noexcept
	{
		return m_Size;
	}

	template <typename T, rox::core::usize PageSize>
	rox::core::usize SlotMap<T, PageSize>::Capacity() const noexcept
	{
		return m_Slots.Capacity();
	}

	template <typename T, rox::core::usize PageSize>
	bool SlotMap<T, PageSize>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <typename T, rox::core::usize PageSize>
	MemoryResource& SlotMap<T, PageSize>::GetResource() const noexcept
	{
		return m_Slots.GetResource();
	}

	template <typename T, rox::core::usize PageSize>
	void SlotMap<T, PageSize>::ReserveRequired(rox::core::usize capacity) noexcept
	{
		m_Slots.ReserveRequired(capacity);
	}

	template <typename T, rox::core::usize PageSize>
	bool SlotMap<T, PageSize>::Reserve(rox::core::usize capacity) noexcept
	{
		return m_Slots.Reserve(capacity);
	}

	template <typename T, rox::core::usize PageSize>
	template <typename... Arguments>
	typename SlotMap<T, PageSize>::Handle SlotMap<T, PageSize>::Emplace(Arguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<T, Arguments&&...>
	{
		const rox::core::usize index = m_FreeHead == NO_SLOT ? m_Slots.Size() : m_FreeHead;
		Slot* slot = m_FreeHead == NO_SLOT ? m_Slots.Emplace() : rox::core::AddressOf(m_Slots[m_FreeHead]);
		if (slot == nullptr)
		{
			return {};
		}
		m_FreeHead = slot->NextFree;
		rox::core::ConstructAt(reinterpret_cast<T*>(slot->Storage), rox::core::Forward<Arguments>(arguments)...);
		slot->Occupied = true;
		++m_Size;
		return Handle(m_Owner, index, slot->Generation);
	}

	template <typename T, rox::core::usize PageSize>
	template <typename... Arguments>
	typename SlotMap<T, PageSize>::Handle SlotMap<T, PageSize>::EmplaceRequired(Arguments&&... arguments) noexcept
	    requires rox::core::IsNothrowConstructible<T, Arguments&&...>
	{
		const rox::core::usize index = m_FreeHead == NO_SLOT ? m_Slots.Size() : m_FreeHead;
		Slot& slot = m_FreeHead == NO_SLOT ? m_Slots.EmplaceRequired() : m_Slots[m_FreeHead];
		m_FreeHead = slot.NextFree;
		rox::core::ConstructAt(reinterpret_cast<T*>(slot.Storage), rox::core::Forward<Arguments>(arguments)...);
		slot.Occupied = true;
		++m_Size;
		return Handle(m_Owner, index, slot.Generation);
	}

	template <typename T, rox::core::usize PageSize>
	ROXELL_FORCE_INLINE T* SlotMap<T, PageSize>::Find(Handle handle) noexcept
	{
		Slot* slot = findSlot(handle);
		return slot != nullptr ? slot->value() : nullptr;
	}

	template <typename T, rox::core::usize PageSize>
	ROXELL_FORCE_INLINE const T* SlotMap<T, PageSize>::Find(Handle handle) const noexcept
	{
		const Slot* slot = findSlot(handle);
		return slot != nullptr ? slot->value() : nullptr;
	}

	template <typename T, rox::core::usize PageSize>
	bool SlotMap<T, PageSize>::Contains(Handle handle) const noexcept
	{
		return findSlot(handle) != nullptr;
	}

	template <typename T, rox::core::usize PageSize>
	T& SlotMap<T, PageSize>::Get(Handle handle) noexcept
	{
		Slot* slot = findSlot(handle);
		ASSERT(slot != nullptr, "Slot map handle must identify a live value.");
		return *slot->value();
	}

	template <typename T, rox::core::usize PageSize>
	const T& SlotMap<T, PageSize>::Get(Handle handle) const noexcept
	{
		const Slot* slot = findSlot(handle);
		ASSERT(slot != nullptr, "Slot map handle must identify a live value.");
		return *slot->value();
	}

	template <typename T, rox::core::usize PageSize>
	bool SlotMap<T, PageSize>::Erase(Handle handle) noexcept
	{
		Slot* slot = findSlot(handle);
		if (slot == nullptr)
		{
			return false;
		}
		eraseSlot(*slot, handle.m_Index);
		return true;
	}

	template <typename T, rox::core::usize PageSize>
	void SlotMap<T, PageSize>::Clear() noexcept
	{
		const rox::core::usize size = m_Slots.Size();
		for (rox::core::usize first = 0; first < size;)
		{
			const rox::core::usize remaining = size - first;
			const rox::core::usize count = remaining < PageSize ? remaining : PageSize;
			Slot* page = rox::core::AddressOf(m_Slots[first]);
			for (rox::core::usize offset = 0; offset < count; ++offset)
			{
				Slot& slot = page[offset];
				if (slot.Occupied)
				{
					eraseSlot(slot, first + offset);
				}
			}
			first += count;
		}
	}

	template <typename T, rox::core::usize PageSize>
	void SlotMap<T, PageSize>::Reset() noexcept
	{
		m_Slots.Reset();
		m_Size = 0;
		m_FreeHead = NO_SLOT;
		m_Owner = Handle::acquireOwner();
	}

	template <typename T, rox::core::usize PageSize>
	template <typename Callback>
	void SlotMap<T, PageSize>::ForEach(Callback&& callback) noexcept
	    requires rox::core::IsNothrowInvocable<Callback&, Handle, T&>
	{
		if (m_Size == 0)
		{
			return;
		}
		const rox::core::usize size = m_Slots.Size();
		for (rox::core::usize first = 0; first < size;)
		{
			const rox::core::usize remaining = size - first;
			const rox::core::usize count = remaining < PageSize ? remaining : PageSize;
			Slot* page = rox::core::AddressOf(m_Slots[first]);
			auto visit = [&](rox::core::usize offset) noexcept
			{
				Slot& slot = page[offset];
				if (slot.Occupied)
				{
					rox::core::Invoke(callback, Handle(m_Owner, first + offset, slot.Generation), *slot.value());
				}
			};
			rox::core::usize offset = 0;
			for (; count - offset >= 4; offset += 4)
			{
				visit(offset);
				visit(offset + 1);
				visit(offset + 2);
				visit(offset + 3);
			}
			for (; offset < count; ++offset)
			{
				visit(offset);
			}
			first += count;
		}
	}

	template <typename T, rox::core::usize PageSize>
	template <typename Callback>
	void SlotMap<T, PageSize>::ForEach(Callback&& callback) const noexcept
	    requires rox::core::IsNothrowInvocable<Callback&, Handle, const T&>
	{
		if (m_Size == 0)
		{
			return;
		}
		const rox::core::usize size = m_Slots.Size();
		for (rox::core::usize first = 0; first < size;)
		{
			const rox::core::usize remaining = size - first;
			const rox::core::usize count = remaining < PageSize ? remaining : PageSize;
			const Slot* page = rox::core::AddressOf(m_Slots[first]);
			auto visit = [&](rox::core::usize offset) noexcept
			{
				const Slot& slot = page[offset];
				if (slot.Occupied)
				{
					rox::core::Invoke(callback, Handle(m_Owner, first + offset, slot.Generation), *slot.value());
				}
			};
			rox::core::usize offset = 0;
			for (; count - offset >= 4; offset += 4)
			{
				visit(offset);
				visit(offset + 1);
				visit(offset + 2);
				visit(offset + 3);
			}
			for (; offset < count; ++offset)
			{
				visit(offset);
			}
			first += count;
		}
	}

	template <typename T, rox::core::usize PageSize>
	ROXELL_FORCE_INLINE const typename SlotMap<T, PageSize>::Slot* SlotMap<T, PageSize>::findSlot(Handle handle) const noexcept
	{
		if (handle.m_Owner != m_Owner || handle.m_Index >= m_Slots.Size())
		{
			return nullptr;
		}
		const Slot& slot = m_Slots[handle.m_Index];
		return slot.Occupied && slot.Generation == handle.m_Generation ? rox::core::AddressOf(slot) : nullptr;
	}

	template <typename T, rox::core::usize PageSize>
	ROXELL_FORCE_INLINE typename SlotMap<T, PageSize>::Slot* SlotMap<T, PageSize>::findSlot(Handle handle) noexcept
	{
		return const_cast<Slot*>(static_cast<const SlotMap&>(*this).findSlot(handle));
	}

	template <typename T, rox::core::usize PageSize>
	void SlotMap<T, PageSize>::eraseSlot(Slot& slot, rox::core::usize index) noexcept
	{
		ASSERT(slot.Generation != rox::core::NumericLimits<rox::core::uint64>::max(),
		       "Slot map generation must remain representable.");
		rox::core::DestroyAt(slot.value());
		slot.Occupied = false;
		++slot.Generation;
		slot.NextFree = m_FreeHead;
		m_FreeHead = index;
		--m_Size;
	}
}
