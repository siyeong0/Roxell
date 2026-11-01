#pragma once

#include <UniquePtr.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>

namespace rox::core
{
	// The complete allocation and its resource outlive every borrowed interface view.
	// Assignments publish incoming ownership before release callbacks, which may reset or replace this owner.
	template <typename T>
	class InterfaceOwner final
	{
		static_assert(IsObject<T> && !IsArray<T>, "InterfaceOwner requires one object type.");
		template <typename> friend class InterfaceOwner;

	private:
		using DestroyFunction = void (*)(MemoryResource&, void*) noexcept;

	public:
		InterfaceOwner() noexcept = default;
		~InterfaceOwner() noexcept { Reset(); }

		InterfaceOwner(const InterfaceOwner&) = delete;
		InterfaceOwner& operator=(const InterfaceOwner&) = delete;

		InterfaceOwner(InterfaceOwner&& other) noexcept { moveFrom(other); }

		InterfaceOwner& operator=(InterfaceOwner&& other) noexcept
		{
			if (this != &other)
			{
				InterfaceOwner previous(core::Move(*this));
				moveFrom(other);
			}

			return *this;
		}

		template <typename U>
		    requires IsConvertible<U*, T*>
		InterfaceOwner(InterfaceOwner<U>&& other) noexcept
		{
			moveFrom(other);
		}

		template <typename U>
		    requires IsConvertible<U*, T*>
		InterfaceOwner& operator=(InterfaceOwner<U>&& other) noexcept
		{
			InterfaceOwner previous(core::Move(*this));
			moveFrom(other);
			return *this;
		}

		template <typename U = T, typename... TArguments>
		    requires(IsConvertible<U*, T*> && !IsConst<U>)
		[[nodiscard]] static InterfaceOwner Create(MemoryResource& resource, TArguments&&... arguments) noexcept
		{
			U* const object = CreateObject<U>(resource, core::Forward<TArguments>(arguments)...);
			InterfaceOwner owner;

			if (object != nullptr)
			{
				owner.m_Resource = &resource;
				owner.m_CompleteObject = object;
				owner.m_View = object;
				owner.m_Destroy = [](MemoryResource& allocationResource, void* completeObject) noexcept
				{
					DestroyObject(allocationResource, static_cast<U*>(completeObject));
				};
			}

			return owner;
		}

		template <typename U = T, typename... TArguments>
		    requires(IsConvertible<U*, T*> && !IsConst<U>)
		[[nodiscard]] static bool CreateInto(MemoryResource& resource, InterfaceOwner* outOwner,
		                                     TArguments&&... arguments) noexcept
		{
			ASSERT(outOwner != nullptr, "InterfaceOwner::CreateInto requires a valid output pointer.");
			InterfaceOwner<T> replacement = Create<U>(resource, core::Forward<TArguments>(arguments)...);
			if (!replacement)
			{
				return false;
			}

			*outOwner = core::Move(replacement);
			return true;
		}

		template <typename U = T, typename... TArguments>
		    requires(IsConvertible<U*, T*> && !IsConst<U>)
		[[nodiscard]] static InterfaceOwner CreateRequired(MemoryResource& resource, TArguments&&... arguments) noexcept
		{
			InterfaceOwner<T> owner = Create<U>(resource, core::Forward<TArguments>(arguments)...);
			if (!owner)
			{
				ReportMemoryFailure(
				    {EMemoryFailureReason::AllocationFailed, &resource, resource.GetName(), {sizeof(U), alignof(U)}});
			}

			return owner;
		}

		void Reset() noexcept
		{
			MemoryResource* const resource = m_Resource;
			void* const object = m_CompleteObject;
			const DestroyFunction destroy = m_Destroy;

			clearOwnership();

			if (object != nullptr)
			{
				destroy(*resource, object);
			}
		}

		[[nodiscard]] T* Get() const noexcept { return m_View; }
		[[nodiscard]] MemoryResource* GetResource() const noexcept { return m_Resource; }

		[[nodiscard]] explicit operator bool() const noexcept { return m_View != nullptr; }

		T* operator->() const noexcept
		{
			ASSERT(m_View != nullptr, "InterfaceOwner cannot access an empty owner.");
			return m_View;
		}

		T& operator*() const noexcept
		{
			ASSERT(m_View != nullptr, "InterfaceOwner cannot dereference an empty owner.");
			return *m_View;
		}

	private:
		template <typename U>
		void moveFrom(InterfaceOwner<U>& other) noexcept
		{
			m_Resource = other.m_Resource;
			m_CompleteObject = other.m_CompleteObject;
			m_View = other.m_View;
			m_Destroy = other.m_Destroy;

			other.clearOwnership();
		}

		void clearOwnership() noexcept
		{
			m_Resource = nullptr;
			m_CompleteObject = nullptr;
			m_View = nullptr;
			m_Destroy = nullptr;
		}

	private:
		MemoryResource* m_Resource = nullptr;
		void* m_CompleteObject = nullptr;

		T* m_View = nullptr;

		DestroyFunction m_Destroy = nullptr;
	};
} // namespace rox::core
