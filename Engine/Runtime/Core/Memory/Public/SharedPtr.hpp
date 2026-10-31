#pragma once

#include <Assertion.h>
#include <Synchronization.hpp>
#include <Types.h>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <MemoryResource.h>

#include <atomic>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace rox::core
{
	template <typename T> class SharedPtr;

	template <typename T, typename... ArgumentTypes>
	[[nodiscard]] bool MakeSharedInto(MemoryResource& resource, SharedPtr<T>* outOwner, ArgumentTypes&&... arguments) noexcept;

	template <typename T, typename... ArgumentTypes>
	[[nodiscard]] SharedPtr<T> MakeSharedRequired(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept;

	// SharedPtr owns one exact T and its atomic strong count in one MemoryResource allocation.
	// The resource must outlive every owner. Different owners may be copied and released concurrently;
	// concurrent access to the same SharedPtr object requires external synchronization.
	// Assignments publish incoming ownership before release callbacks, which may reset or replace this owner.
	template <typename T>
	class SharedPtr final
	{
		static_assert(rox::core::IsObject<T>, "SharedPtr requires an object type.");
		static_assert(!rox::core::IsArray<T>, "SharedPtr does not support arrays.");

		class ControlBlock final
		{
		public:
			explicit ControlBlock(MemoryResource& resource) noexcept
			    : m_OwnerCount(1)
			    , m_Resource(resource)
			{
			}

		public:
			rox::core::Atomic<rox::core::usize> m_OwnerCount;
			MemoryResource& m_Resource;
		};

		// Defer the object layout until an operation needs a complete T. This lets
		// recursive immutable values declare their owner before defining its payload.
		struct AllocationLayout final
		{
			static_assert(rox::core::IsNothrowDestructible<T>, "SharedPtr requires nothrow destruction.");
			static_assert(sizeof(ControlBlock) <= rox::core::NumericLimits<rox::core::usize>::max() - (alignof(T) - 1), "SharedPtr control-block alignment calculation cannot overflow.");
			static constexpr rox::core::usize OBJECT_OFFSET = (sizeof(ControlBlock) + alignof(T) - 1) & ~(alignof(T) - 1);
			static_assert(sizeof(T) <= rox::core::NumericLimits<rox::core::usize>::max() - OBJECT_OFFSET, "SharedPtr allocation size cannot overflow.");
			static constexpr rox::core::usize ALLOCATION_BYTE_COUNT = OBJECT_OFFSET + sizeof(T);
			static constexpr rox::core::usize ALLOCATION_ALIGNMENT = alignof(ControlBlock) > alignof(T) ? alignof(ControlBlock) : alignof(T);
		};

	public:
		template <typename... ArgumentTypes>
		[[nodiscard]] static SharedPtr Create(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
		{
			SharedPtr owner;
			(void)CreateInto(resource, &owner, rox::core::Forward<ArgumentTypes>(arguments)...);
			return owner;
		}

		template <typename... ArgumentTypes>
		[[nodiscard]] static bool CreateInto(MemoryResource& resource, SharedPtr* const outOwner, ArgumentTypes&&... arguments) noexcept
		{
			ASSERT(outOwner != nullptr, "SharedPtr::CreateInto requires a valid output pointer.");
			static_assert(rox::core::IsNothrowConstructible<T, ArgumentTypes...>, "SharedPtr::CreateInto requires nothrow object construction.");

			void* const memory = resource.Allocate({AllocationLayout::ALLOCATION_BYTE_COUNT, AllocationLayout::ALLOCATION_ALIGNMENT});
			if (memory == nullptr)
			{
				return false;
			}

			ControlBlock* const controlBlock = ::new (memory) ControlBlock(resource);
			void* const objectStorage = static_cast<rox::core::Byte*>(memory) + AllocationLayout::OBJECT_OFFSET;
			::new (objectStorage) T(rox::core::Forward<ArgumentTypes>(arguments)...);

			SharedPtr replacement(controlBlock);
			*outOwner = rox::core::Move(replacement);
			return true;
		}

		template <typename... ArgumentTypes>
		[[nodiscard]] static SharedPtr CreateRequired(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
		{
			static_assert(rox::core::IsNothrowConstructible<T, ArgumentTypes...>, "SharedPtr::CreateRequired requires nothrow object construction.");

			void* const memory = resource.AllocateRequired({AllocationLayout::ALLOCATION_BYTE_COUNT, AllocationLayout::ALLOCATION_ALIGNMENT});
			ControlBlock* const controlBlock = ::new (memory) ControlBlock(resource);
			void* const objectStorage = static_cast<rox::core::Byte*>(memory) + AllocationLayout::OBJECT_OFFSET;
			::new (objectStorage) T(rox::core::Forward<ArgumentTypes>(arguments)...);

			return SharedPtr(controlBlock);
		}

		SharedPtr() noexcept
		    : m_ControlBlock(nullptr)
		{
		}

		~SharedPtr() { Reset(); }

		SharedPtr(const SharedPtr& other) noexcept
		    : m_ControlBlock(other.m_ControlBlock)
		{
			retain(m_ControlBlock);
		}

		SharedPtr& operator=(const SharedPtr& other) noexcept
		{
			if (this != &other)
			{
				ControlBlock* const replacement = other.m_ControlBlock;
				retain(replacement);
				SharedPtr previous(rox::core::Move(*this));
				m_ControlBlock = replacement;
			}

			return *this;
		}

		SharedPtr(SharedPtr&& other) noexcept
		    : m_ControlBlock(other.m_ControlBlock)
		{
			other.m_ControlBlock = nullptr;
		}

		SharedPtr& operator=(SharedPtr&& other) noexcept
		{
			if (this != &other)
			{
				SharedPtr previous(rox::core::Move(*this));
				m_ControlBlock = other.m_ControlBlock;
				other.m_ControlBlock = nullptr;
			}

			return *this;
		}

		void Reset() noexcept
		{
			ControlBlock* const released = m_ControlBlock;
			m_ControlBlock = nullptr;
			release(released);
		}

		[[nodiscard]] T* Get() noexcept { return getObject(m_ControlBlock); }
		[[nodiscard]] T* Get() const noexcept { return getObject(m_ControlBlock); }

		[[nodiscard]] MemoryResource* GetResource() const noexcept
		{
			return m_ControlBlock == nullptr ? nullptr : &m_ControlBlock->m_Resource;
		}

		[[nodiscard]] rox::core::usize UseCount() const noexcept
		{
			return m_ControlBlock == nullptr ? 0 : m_ControlBlock->m_OwnerCount.load(rox::core::MemoryOrderAcquire);
		}

		T& operator*() noexcept
		{
			ASSERT(m_ControlBlock != nullptr, "SharedPtr cannot dereference a null object.");
			return *getObject(m_ControlBlock);
		}

		T& operator*() const noexcept
		{
			ASSERT(m_ControlBlock != nullptr, "SharedPtr cannot dereference a null object.");
			return *getObject(m_ControlBlock);
		}

		T* operator->() noexcept
		{
			ASSERT(m_ControlBlock != nullptr, "SharedPtr cannot access a null object.");
			return getObject(m_ControlBlock);
		}

		T* operator->() const noexcept
		{
			ASSERT(m_ControlBlock != nullptr, "SharedPtr cannot access a null object.");
			return getObject(m_ControlBlock);
		}

		explicit operator bool() const noexcept { return m_ControlBlock != nullptr; }
		[[nodiscard]] bool operator==(Nullptr) const noexcept { return m_ControlBlock == nullptr; }
		[[nodiscard]] bool operator==(const SharedPtr& other) const noexcept { return m_ControlBlock == other.m_ControlBlock; }

	private:
		explicit SharedPtr(ControlBlock* const controlBlock) noexcept
		    : m_ControlBlock(controlBlock)
		{
		}

		static T* getObject(ControlBlock* const controlBlock) noexcept
		{
			if (controlBlock == nullptr)
			{
				return nullptr;
			}

			rox::core::Byte* const objectStorage = reinterpret_cast<rox::core::Byte*>(controlBlock) + AllocationLayout::OBJECT_OFFSET;
			return rox::core::Launder(reinterpret_cast<T*>(objectStorage));
		}

		static void retain(ControlBlock* const controlBlock) noexcept
		{
			if (controlBlock == nullptr)
			{
				return;
			}

			rox::core::usize ownerCount = controlBlock->m_OwnerCount.load(rox::core::MemoryOrderRelaxed);

			for (;;)
			{
				ASSERT(ownerCount != 0, "SharedPtr cannot retain a released control block.");
				ASSERT(ownerCount != rox::core::NumericLimits<rox::core::usize>::max(), "SharedPtr owner count cannot overflow.");

				if (controlBlock->m_OwnerCount.compare_exchange_weak(ownerCount, ownerCount + 1, rox::core::MemoryOrderRelaxed, rox::core::MemoryOrderRelaxed))
				{
					return;
				}
			}
		}

		static void release(ControlBlock* const controlBlock) noexcept
		{
			if (controlBlock == nullptr)
			{
				return;
			}

			const rox::core::usize previousOwnerCount = controlBlock->m_OwnerCount.fetch_sub(1, rox::core::MemoryOrderAcqRel);
			ASSERT(previousOwnerCount != 0, "SharedPtr owner count cannot underflow.");

			if (previousOwnerCount != 1)
			{
				return;
			}

			MemoryResource& resource = controlBlock->m_Resource;
			T* const object = getObject(controlBlock);
			object->~T();
			controlBlock->~ControlBlock();
			resource.Deallocate(controlBlock, {AllocationLayout::ALLOCATION_BYTE_COUNT, AllocationLayout::ALLOCATION_ALIGNMENT});
		}

	private:
		ControlBlock* m_ControlBlock;
	};

	template <typename T, typename... ArgumentTypes>
	bool MakeSharedInto(MemoryResource& resource, SharedPtr<T>* const outOwner, ArgumentTypes&&... arguments) noexcept
	{
		return SharedPtr<T>::CreateInto(resource, outOwner, rox::core::Forward<ArgumentTypes>(arguments)...);
	}

	template <typename T, typename... ArgumentTypes>
	SharedPtr<T> MakeSharedRequired(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
	{
		return SharedPtr<T>::CreateRequired(resource, rox::core::Forward<ArgumentTypes>(arguments)...);
	}

	template <typename T, typename... ArgumentTypes>
	[[nodiscard]] SharedPtr<T> MakeShared(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
	{
		return SharedPtr<T>::Create(resource, rox::core::Forward<ArgumentTypes>(arguments)...);
	}
} // namespace rox::core
