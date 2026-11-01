#pragma once

#include <Assertion.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <FunctionTarget.hpp>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace rox::core
{
	template <typename TSignature> class FunctionRef;

	// The target outlives every invocation. Copying this view does not extend its lifetime.
	template <typename TResult, typename... TArguments> class FunctionRef<TResult(TArguments...) noexcept> final
	{
	private:
		using FunctionPointer = TResult (*)(TArguments...) noexcept;

		union Binding
		{
			template <typename TTarget> explicit Binding(TTarget* object) noexcept
			{
				if constexpr (IsConst<TTarget> && IsVolatile<TTarget>)
				{
					ConstVolatileObject = object;
				}
				else if constexpr (IsConst<TTarget>)
				{
					ConstObject = object;
				}
				else if constexpr (IsVolatile<TTarget>)
				{
					VolatileObject = object;
				}
				else
				{
					Object = object;
				}
			}

			explicit Binding(FunctionPointer function) noexcept
			    : Function(function)
			{
			}

			void* Object;
			const void* ConstObject;
			volatile void* VolatileObject;
			const volatile void* ConstVolatileObject;
			FunctionPointer Function;
		};

	public:
		FunctionRef(FunctionPointer function) noexcept
		    : m_Binding(function)
		    , m_Invoke(&invokeFunction)
		{
			ASSERT(function != nullptr, "FunctionRef requires a non-null function pointer.");
		}

		template <typename TTarget>
		FunctionRef(TTarget& target) noexcept
		    requires(!IsSame<RemoveCv<TTarget>, FunctionRef> && IsObject<TTarget> &&
		             !IsMemberPointer<TTarget> && !IsFunction<RemovePointer<TTarget>> &&
		             NothrowFunctionTarget<TResult, TTarget, TArguments...>)
		    : m_Binding(core::AddressOf(target))
		    , m_Invoke(&invokeObject<TTarget>)
		{
		}

		FunctionRef(const FunctionRef&) noexcept = default;
		FunctionRef(FunctionRef&&) noexcept = default;
		FunctionRef& operator=(const FunctionRef&) noexcept = default;
		FunctionRef& operator=(FunctionRef&&) noexcept = default;

		TResult operator()(TArguments... arguments) const noexcept
		{
			return m_Invoke(m_Binding, core::Forward<TArguments>(arguments)...);
		}

	private:
		template <typename TTarget> static TTarget* getObject(Binding binding) noexcept
		{
			if constexpr (IsConst<TTarget> && IsVolatile<TTarget>)
			{
				return static_cast<TTarget*>(binding.ConstVolatileObject);
			}
			else if constexpr (IsConst<TTarget>)
			{
				return static_cast<TTarget*>(binding.ConstObject);
			}
			else if constexpr (IsVolatile<TTarget>)
			{
				return static_cast<TTarget*>(binding.VolatileObject);
			}
			else
			{
				return static_cast<TTarget*>(binding.Object);
			}
		}

		template <typename TTarget> static TResult invokeObject(Binding binding, TArguments&&... arguments) noexcept
		{
			if constexpr (IsVoid<TResult>)
			{
				core::Invoke(*getObject<TTarget>(binding), core::Forward<TArguments>(arguments)...);
			}
			else
			{
				return core::Invoke(*getObject<TTarget>(binding), core::Forward<TArguments>(arguments)...);
			}
		}

		static TResult invokeFunction(Binding binding, TArguments&&... arguments) noexcept
		{
			return binding.Function(core::Forward<TArguments>(arguments)...);
		}

	private:
		Binding m_Binding;
		TResult (*m_Invoke)(Binding, TArguments&&...) noexcept;
	};
}
