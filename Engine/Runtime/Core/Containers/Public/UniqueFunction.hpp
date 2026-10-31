#pragma once

#include <FunctionTarget.hpp>
#include <TypeVocabulary.hpp>
#include <Types.h>
#include <MemoryOperations.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <UniquePtr.hpp>

#include <functional>
#include <type_traits>
#include <utility>

namespace rox::core
{
	template <typename TSignature> class UniqueFunction;

	// The resource outlives the target. Moving transfers the resource with the target ownership.
	template <typename TResult, typename... TArguments>
	class UniqueFunction<TResult(TArguments...) noexcept> final
	{
	public:
		static constexpr rox::core::usize INLINE_CAPACITY = 64;

	public:
		UniqueFunction() noexcept = default;

		~UniqueFunction() noexcept;

		template <typename TTarget>
		UniqueFunction(MemoryResource& resource, TTarget&& target) noexcept
		    requires(!rox::core::IsSame<rox::core::Decay<TTarget>, UniqueFunction> &&
		             !rox::core::IsMemberPointer<rox::core::Decay<TTarget>> &&
		             rox::core::IsNothrowConstructible<rox::core::Decay<TTarget>, TTarget &&> &&
		             rox::core::IsNothrowDestructible<rox::core::Decay<TTarget>> &&
		             NothrowFunctionTarget<TResult, rox::core::Decay<TTarget>, TArguments...>);

		UniqueFunction(UniqueFunction&& other) noexcept;

		UniqueFunction& operator=(UniqueFunction&& other) noexcept;
		UniqueFunction(const UniqueFunction&) = delete;
		UniqueFunction& operator=(const UniqueFunction&) = delete;
		explicit operator bool() const noexcept;

		MemoryResource* GetResource() const noexcept;

		// Heap allocation failure preserves this owner and does not consume the supplied target.
		// Successful replacement transfers the supplied resource with its target.
		template <typename TTarget>
		[[nodiscard]] bool Assign(MemoryResource& resource, TTarget&& target) noexcept
		    requires rox::core::IsConstructible<UniqueFunction, MemoryResource&, TTarget&&>;

		// Detach before destruction so target cleanup may observe or reset this empty owner.
		void Reset() noexcept;

		TResult operator()(TArguments... arguments) & noexcept;

	private:
		template <typename TTarget> static void destroyInline(MemoryResource&, void* object) noexcept;
		template <typename TTarget> static void* moveInline(void* source, void* destination) noexcept;
		template <typename TTarget> static TResult invokeTarget(void* object, TArguments&&... arguments) noexcept;
		template <typename TTarget> static void destroyTarget(MemoryResource& resource, void* object) noexcept;

		void moveFrom(UniqueFunction& other) noexcept;

	private:
		MemoryResource* m_Resource = nullptr;
		void* m_Object = nullptr;

		TResult (*m_Invoke)(void*, TArguments&&...) noexcept = nullptr;
		void (*m_Destroy)(MemoryResource&, void*) noexcept = nullptr;
		void* (*m_Move)(void*, void*) noexcept = nullptr;

		alignas(rox::core::MaxAlign) rox::core::Byte m_Inline[INLINE_CAPACITY];
	};
}

namespace rox::core
{
	template <typename TResult, typename... TArguments>
	UniqueFunction<TResult(TArguments...) noexcept>::~UniqueFunction() noexcept
	{
		Reset();
	}

	template <typename TResult, typename... TArguments>
	template <typename TTarget>
	UniqueFunction<TResult(TArguments...) noexcept>::UniqueFunction(MemoryResource& resource, TTarget&& target) noexcept
	    requires(!rox::core::IsSame<rox::core::Decay<TTarget>, UniqueFunction> &&
	             !rox::core::IsMemberPointer<rox::core::Decay<TTarget>> &&
	             rox::core::IsNothrowConstructible<rox::core::Decay<TTarget>, TTarget &&> &&
	             rox::core::IsNothrowDestructible<rox::core::Decay<TTarget>> &&
	             NothrowFunctionTarget<TResult, rox::core::Decay<TTarget>, TArguments...>)
	{
		using Target = rox::core::Decay<TTarget>;
		if constexpr (rox::core::IsPointer<Target> && rox::core::IsFunction<rox::core::RemovePointer<Target>>)
		{
			ASSERT(target != nullptr, "UniqueFunction requires a non-null function pointer.");
		}

		if constexpr (sizeof(Target) <= INLINE_CAPACITY && alignof(Target) <= alignof(rox::core::MaxAlign) &&
		              rox::core::IsNothrowMoveConstructible<Target>)
		{
			m_Object = rox::core::ConstructAt(reinterpret_cast<Target*>(m_Inline), rox::core::Forward<TTarget>(target));
			m_Destroy = &destroyInline<Target>;
			m_Move = &moveInline<Target>;
		}
		else
		{
			m_Object = CreateObjectRequired<Target>(resource, rox::core::Forward<TTarget>(target));
			m_Destroy = &destroyTarget<Target>;
		}

		m_Resource = &resource;
		m_Invoke = &invokeTarget<Target>;
	}

	template <typename TResult, typename... TArguments>
	UniqueFunction<TResult(TArguments...) noexcept>::UniqueFunction(UniqueFunction&& other) noexcept
	{
		moveFrom(other);
	}

	template <typename TResult, typename... TArguments>
	UniqueFunction<TResult(TArguments...) noexcept>& UniqueFunction<TResult(TArguments...) noexcept>::operator=(UniqueFunction&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			moveFrom(other);
		}

		return *this;
	}

	template <typename TResult, typename... TArguments>
	UniqueFunction<TResult(TArguments...) noexcept>::operator bool() const noexcept
	{
		return m_Object != nullptr;
	}

	template <typename TResult, typename... TArguments>
	MemoryResource* UniqueFunction<TResult(TArguments...) noexcept>::GetResource() const noexcept
	{
		return m_Resource;
	}

	template <typename TResult, typename... TArguments>
	template <typename TTarget>
	bool UniqueFunction<TResult(TArguments...) noexcept>::Assign(MemoryResource& resource, TTarget&& target) noexcept
	    requires rox::core::IsConstructible<UniqueFunction, MemoryResource&, TTarget&&>
	{
		using Target = rox::core::Decay<TTarget>;

		if constexpr (sizeof(Target) <= INLINE_CAPACITY && alignof(Target) <= alignof(rox::core::MaxAlign) && rox::core::IsNothrowMoveConstructible<Target>)
		{
			UniqueFunction replacement(resource, rox::core::Forward<TTarget>(target));
			*this = rox::core::Move(replacement);
		}
		else
		{
			Target* const object = CreateObject<Target>(resource, rox::core::Forward<TTarget>(target));

			if (object == nullptr)
			{
				return false;
			}

			UniqueFunction replacement;
			replacement.m_Object = object;
			replacement.m_Resource = &resource;
			replacement.m_Invoke = &invokeTarget<Target>;
			replacement.m_Destroy = &destroyTarget<Target>;
			*this = rox::core::Move(replacement);
		}

		return true;
	}

	template <typename TResult, typename... TArguments>
	void UniqueFunction<TResult(TArguments...) noexcept>::Reset() noexcept
	{
		auto* const resource = rox::core::Exchange(m_Resource, nullptr);
		void* const object = rox::core::Exchange(m_Object, nullptr);
		const auto destroy = rox::core::Exchange(m_Destroy, nullptr);
		m_Invoke = nullptr;
		m_Move = nullptr;

		if (object != nullptr)
		{
			destroy(*resource, object);
		}
	}

	template <typename TResult, typename... TArguments>
	TResult UniqueFunction<TResult(TArguments...) noexcept>::operator()(TArguments... arguments) & noexcept
	{
		ASSERT(m_Object != nullptr, "UniqueFunction cannot invoke an empty target.");
		return m_Invoke(m_Object, rox::core::Forward<TArguments>(arguments)...);
	}

	template <typename TResult, typename... TArguments>
	template <typename TTarget>
	void UniqueFunction<TResult(TArguments...) noexcept>::destroyInline(MemoryResource&, void* object) noexcept
	{
		rox::core::DestroyAt(static_cast<TTarget*>(object));
	}

	template <typename TResult, typename... TArguments>
	template <typename TTarget>
	void* UniqueFunction<TResult(TArguments...) noexcept>::moveInline(void* source, void* destination) noexcept
	{
		auto* target = static_cast<TTarget*>(source);
		auto* moved = rox::core::ConstructAt(static_cast<TTarget*>(destination), rox::core::Move(*target));
		rox::core::DestroyAt(target);
		return moved;
	}

	template <typename TResult, typename... TArguments>
	template <typename TTarget>
	TResult UniqueFunction<TResult(TArguments...) noexcept>::invokeTarget(void* object, TArguments&&... arguments) noexcept
	{
		if constexpr (rox::core::IsVoid<TResult>)
		{
			rox::core::Invoke(*static_cast<TTarget*>(object), rox::core::Forward<TArguments>(arguments)...);
		}
		else
		{
			return rox::core::Invoke(*static_cast<TTarget*>(object), rox::core::Forward<TArguments>(arguments)...);
		}
	}

	template <typename TResult, typename... TArguments>
	template <typename TTarget>
	void UniqueFunction<TResult(TArguments...) noexcept>::destroyTarget(MemoryResource& resource, void* object) noexcept
	{
		DestroyObject(resource, static_cast<TTarget*>(object));
	}

	template <typename TResult, typename... TArguments>
	void UniqueFunction<TResult(TArguments...) noexcept>::moveFrom(UniqueFunction& other) noexcept
	{
		m_Resource = rox::core::Exchange(other.m_Resource, nullptr);
		void* const object = rox::core::Exchange(other.m_Object, nullptr);
		m_Invoke = rox::core::Exchange(other.m_Invoke, nullptr);
		m_Destroy = rox::core::Exchange(other.m_Destroy, nullptr);
		m_Move = rox::core::Exchange(other.m_Move, nullptr);
		m_Object = m_Move != nullptr ? m_Move(object, m_Inline) : object;
	}
}
