#pragma once

#include <PoolMemoryResource.h>
#include <Types.h>
#include <MemoryOperations.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <UniquePtr.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// Objects retain stable addresses until destruction. Constructors and destructors must not re-enter this pool.
	template <typename T>
	class ObjectPool final
	{
		static_assert(IsObject<T> && !IsArray<T> && IsSame<T, RemoveCv<T>>, "ObjectPool requires an unqualified non-array object type.");
		static_assert(IsNothrowDestructible<T>, "Pooled objects must have nothrow destruction.");

	public:
		explicit ObjectPool(MemoryResource& upstream) noexcept : m_Storage(upstream) {}

		[[nodiscard]] bool Initialize(usize capacity) noexcept { return m_Storage.Initialize(sizeof(T), alignof(T), capacity); }
		[[nodiscard]] bool IsInitialized() const noexcept { return m_Storage.IsInitialized(); }

		~ObjectPool() noexcept { Clear(); }

		ObjectPool(const ObjectPool&) = delete;
		ObjectPool& operator=(const ObjectPool&) = delete;
		ObjectPool(ObjectPool&&) = delete;
		ObjectPool& operator=(ObjectPool&&) = delete;

		template <typename... TArguments>
		T& EmplaceRequired(TArguments&&... arguments) noexcept
		{
			return *CreateObjectRequired<T>(m_Storage, core::Forward<TArguments>(arguments)...);
		}

		template <typename... TArguments>
		[[nodiscard]] T* Emplace(TArguments&&... arguments) noexcept
		{
			return CreateObject<T>(m_Storage, core::Forward<TArguments>(arguments)...);
		}

		void Destroy(T& object) noexcept
		{
			T* const storage = core::AddressOf(object);
			const usize index = m_Storage.liveSlotIndex(storage);
			core::DestroyAt(storage);
			m_Storage.releaseObjectSlot(index);
		}

		void Clear() noexcept
		{
			usize index = 0;
			while (!IsEmpty())
			{
				T* const storage = static_cast<T*>(m_Storage.nextLiveSlotStorage(&index));
				core::DestroyAt(storage);
				m_Storage.releaseObjectSlot(index);
				++index;
			}
		}

		[[nodiscard]] usize Size() const noexcept { return m_Storage.GetLiveBlockCount(); }
		[[nodiscard]] usize Capacity() const noexcept { return m_Storage.GetBlockCapacity(); }

		[[nodiscard]] bool IsEmpty() const noexcept { return Size() == 0; }

	private:
		PoolMemoryResource m_Storage;
	};
} // namespace rox::core
