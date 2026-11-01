#pragma once

#include <Assertion.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <MemoryResource.h>
#include <MemoryOperations.hpp>

#include <new>
#include <type_traits>
#include <utility>

namespace rox::core
{
	template <typename T> class UniquePtr;

	template <typename T, typename... ArgumentTypes>
	[[nodiscard]] bool ConstructUniqueInto(MemoryResource& resource, UniquePtr<T>* outOwner, ArgumentTypes&&... arguments);

	template <typename T, typename... ArgumentTypes>
	[[nodiscard]] T* CreateObject(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
	{
		static_assert(IsNothrowConstructible<T, ArgumentTypes...>);
		static_assert(IsNothrowDestructible<T>);

		void* const memory = resource.Allocate({sizeof(T), alignof(T)});
		if (memory == nullptr)
		{
			return nullptr;
		}

		return ::new (memory) T(core::Forward<ArgumentTypes>(arguments)...);
	}

	template <typename T, typename... ArgumentTypes>
	[[nodiscard]] T* CreateObjectRequired(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
	{
		static_assert(IsNothrowConstructible<T, ArgumentTypes...>);
		static_assert(IsNothrowDestructible<T>);

		void* const memory = resource.AllocateRequired({sizeof(T), alignof(T)});
		return ::new (memory) T(core::Forward<ArgumentTypes>(arguments)...);
	}

	template <typename T>
	void DestroyObject(MemoryResource& resource, T* const object) noexcept
	{
		static_assert(IsNothrowDestructible<T>);

		if (object == nullptr)
		{
			return;
		}

		object->~T();
		resource.Deallocate(object, {sizeof(T), alignof(T)});
	}

	template <typename T>
	class UniquePtr final
	{
	public:
		template <typename... ArgumentTypes>
		[[nodiscard]] static UniquePtr Create(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
		{
			return UniquePtr(resource, CreateObject<T>(resource, core::Forward<ArgumentTypes>(arguments)...));
		}

		template <typename... ArgumentTypes>
		[[nodiscard]] static UniquePtr CreateRequired(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
		{
			return UniquePtr(resource, CreateObjectRequired<T>(resource, core::Forward<ArgumentTypes>(arguments)...));
		}

		template <typename... ArgumentTypes>
		[[nodiscard]] static bool CreateInto(MemoryResource& resource, UniquePtr* outOwner, ArgumentTypes&&... arguments) noexcept
		{
			ASSERT(outOwner != nullptr, "UniquePtr::CreateInto requires a valid output pointer.");
			UniquePtr<T> replacement = Create(resource, core::Forward<ArgumentTypes>(arguments)...);
			if (!replacement)
			{
				return false;
			}

			*outOwner = core::Move(replacement);
			return true;
		}

		// Throwing construction publishes the replacement only after success.
		template <typename... ArgumentTypes>
		[[nodiscard]] static bool ConstructInto(MemoryResource& resource, UniquePtr* const outOwner, ArgumentTypes&&... arguments)
		{
			ASSERT(outOwner != nullptr, "UniquePtr::ConstructInto requires a valid output pointer.");
			static_assert(IsNothrowDestructible<T>);

			void* const memory = resource.Allocate({sizeof(T), alignof(T)});
			if (memory == nullptr)
			{
				return false;
			}

			try
			{
				T* const object = ::new (memory) T(core::Forward<ArgumentTypes>(arguments)...);
				UniquePtr replacement(resource, object);
				*outOwner = core::Move(replacement);
				return true;
			}
			catch (...)
			{
				resource.Deallocate(memory, {sizeof(T), alignof(T)});
				throw;
			}
		}

		// The associated resource is transferred with ownership and must outlive the object.
		UniquePtr() noexcept
		    : m_Resource(nullptr)
		    , m_Object(nullptr)
		{
		}

		~UniquePtr() { Reset(); }

		UniquePtr(UniquePtr&& other) noexcept
		    : m_Resource(other.m_Resource)
		    , m_Object(other.m_Object)
		{
			other.m_Resource = nullptr;
			other.m_Object = nullptr;
		}

		// Publish incoming ownership before destruction; release callbacks may reset or replace this owner.
		UniquePtr& operator=(UniquePtr&& other) noexcept
		{
			if (this != &other)
			{
				UniquePtr previous(core::Move(*this));
				m_Resource = other.m_Resource;
				m_Object = other.m_Object;
				other.m_Resource = nullptr;
				other.m_Object = nullptr;
			}

			return *this;
		}

		UniquePtr(const UniquePtr&) = delete;
		UniquePtr& operator=(const UniquePtr&) = delete;

		// Detach before destruction so callbacks observe an empty owner and may safely reset it again.
		void Reset() noexcept
		{
			ASSERT((m_Resource == nullptr) == (m_Object == nullptr), "UniquePtr must keep its resource and object ownership paired.");
			MemoryResource* const resource = core::Exchange(m_Resource, nullptr);
			T* const object = core::Exchange(m_Object, nullptr);

			if (resource != nullptr)
			{
				DestroyObject(*resource, object);
			}
		}

		T* Get() noexcept { return m_Object; }
		T* Get() const noexcept { return m_Object; }

		MemoryResource* GetResource() const noexcept { return m_Resource; }

		T& operator*() noexcept
		{
			ASSERT(m_Object != nullptr, "UniquePtr cannot dereference a null object.");
			return *m_Object;
		}

		T& operator*() const noexcept
		{
			ASSERT(m_Object != nullptr, "UniquePtr cannot dereference a null object.");
			return *m_Object;
		}

		T* operator->() noexcept
		{
			ASSERT(m_Object != nullptr, "UniquePtr cannot access a null object.");
			return m_Object;
		}

		T* operator->() const noexcept
		{
			ASSERT(m_Object != nullptr, "UniquePtr cannot access a null object.");
			return m_Object;
		}

		explicit operator bool() const noexcept { return m_Object != nullptr; }

	private:
		friend struct Relocation<UniquePtr<T>>;

		UniquePtr(MemoryResource& resource, T* const object) noexcept
		    : m_Resource(object == nullptr ? nullptr : &resource)
		    , m_Object(object)
		{
		}

	private:
		MemoryResource* m_Resource;
		T* m_Object;
	};

	template <typename T>
	struct Relocation<UniquePtr<T>>
	{
		static constexpr bool IsCustomized = true;
		static void IntoUninitialized(UniquePtr<T>* destination, UniquePtr<T>* source) noexcept
		{
			::new (static_cast<void*>(destination)) UniquePtr<T>();
			destination->m_Resource = source->m_Resource;
			destination->m_Object = source->m_Object;
			// The source is retired, never read or destroyed again. Its lifetime ends when
			// its slot is reused for another owner or the backing storage is released.
		}
	};

	template <typename T, typename... ArgumentTypes>
	UniquePtr<T> MakeUnique(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
	{
		return UniquePtr<T>::Create(resource, core::Forward<ArgumentTypes>(arguments)...);
	}

	template <typename T, typename... ArgumentTypes>
	UniquePtr<T> MakeUniqueRequired(MemoryResource& resource, ArgumentTypes&&... arguments) noexcept
	{
		return UniquePtr<T>::CreateRequired(resource, core::Forward<ArgumentTypes>(arguments)...);
	}

	// Use only while a current owner still has a throwing constructor. The output changes after success.
	template <typename T, typename... ArgumentTypes>
	bool ConstructUniqueInto(MemoryResource& resource, UniquePtr<T>* const outOwner, ArgumentTypes&&... arguments)
	{
		return UniquePtr<T>::ConstructInto(resource, outOwner, core::Forward<ArgumentTypes>(arguments)...);
	}

} // namespace rox::core
