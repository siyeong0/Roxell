#pragma once

#include <type_traits>
#include <TypeVocabulary.hpp>
#include <TypeTraits.hpp>

namespace rox::core
{
	template <typename TResult, typename TTarget, typename... TArguments>
	concept NothrowFunctionTarget =
	    rox::core::IsNothrowInvocableR<TResult, TTarget&, TArguments...> &&
	    (!rox::core::IsReference<TResult> ||
	     (rox::core::IsReference<rox::core::InvokeResult<TTarget&, TArguments...>> &&
	      rox::core::IsLvalueReference<rox::core::InvokeResult<TTarget&, TArguments...>> == rox::core::IsLvalueReference<TResult> &&
	      rox::core::IsConvertible<
	          rox::core::AddPointer<rox::core::RemoveReference<rox::core::InvokeResult<TTarget&, TArguments...>>>,
	          rox::core::AddPointer<rox::core::RemoveReference<TResult>>>));
}
