#pragma once

#include <type_traits>
#include <utility>

namespace rox::core
{
	template <typename T, T Value> using IntegralConstant = std::integral_constant<T, Value>;

	using TrueType = std::true_type;
	using FalseType = std::false_type;

	template <typename T> using Decay = std::decay_t<T>;

	template <bool B, typename T, typename U> using Conditional = std::conditional_t<B, T, U>;

	template <typename T, typename... A> using InvokeResult = std::invoke_result_t<T, A...>;

	template <typename T, unsigned I = 0> inline constexpr auto Extent = std::extent_v<T, I>;
	template <typename T> inline constexpr auto Rank = std::rank_v<T>;

	template <typename T> auto Declval() noexcept -> decltype(std::declval<T>());
}
