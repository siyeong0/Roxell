#pragma once

#include <type_traits>
#include <TypeVocabulary.hpp>
#include <TypeTraits.hpp>

namespace rox::core
{
	template <typename TResult, typename TTarget, typename... TArguments>
	concept NothrowFunctionTarget =
	    IsNothrowInvocableR<TResult, TTarget&, TArguments...> &&
	    (!IsReference<TResult> ||
	     (IsReference<core::InvokeResult<TTarget&, TArguments...>> &&
	      IsLvalueReference<core::InvokeResult<TTarget&, TArguments...>> == IsLvalueReference<TResult> &&
	      IsConvertible<
	          AddPointer<RemoveReference<core::InvokeResult<TTarget&, TArguments...>>>,
	          AddPointer<RemoveReference<TResult>>>));
}
