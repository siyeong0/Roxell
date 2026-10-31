#pragma once

#include <array>
#include <optional>
#include <variant>
#include <tuple>
#include <initializer_list>
#include <utility>

namespace rox::core
{
	template <typename F, typename T> constexpr decltype(auto) Apply(F&& function, T&& values)
	{
		return std::apply(std::forward<F>(function), std::forward<T>(values));
	}

	template <typename T, std::size_t N> using StaticArray = std::array<T, N>;

	template <typename T> using Optional = std::optional<T>;
	template <typename... T> using Variant = std::variant<T...>;

	template <typename... T> using Tuple = std::tuple<T...>;
	template <typename T, typename U> using Pair = std::pair<T, U>;

	template <typename T> using InitializerList = std::initializer_list<T>;

	template <std::size_t I, typename T> using TupleElement = std::tuple_element_t<I, T>;

	template <std::size_t... I> using IndexSequence = std::index_sequence<I...>;
	template <std::size_t N> using MakeIndexSequence = std::make_index_sequence<N>;
	template <typename... T> using IndexSequenceFor = std::index_sequence_for<T...>;

	template <typename T> using InPlaceTypeTag = std::in_place_type_t<T>;
	template <typename T> inline constexpr auto InPlaceType = std::in_place_type<T>;

	template <std::size_t I> using InPlaceIndexTag = std::in_place_index_t<I>;
	template <std::size_t I> inline constexpr auto InPlaceIndex = std::in_place_index<I>;

	inline constexpr auto InPlace = std::in_place;

	inline constexpr auto NullOptional = std::nullopt;

	using EmptyValue = std::monostate;

	template <typename... TTypes, typename... TArgs>
	constexpr auto Get(TArgs&&... args) noexcept(noexcept(std::get<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::get<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::get<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <std::size_t I, typename T>
	constexpr decltype(auto) Get(T&& value) noexcept(noexcept(std::get<I>(std::forward<T>(value))))
	{
		return std::get<I>(std::forward<T>(value));
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto GetIf(TArgs&&... args) noexcept(noexcept(std::get_if<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::get_if<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::get_if<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <std::size_t I, typename T>
	constexpr decltype(auto) GetIf(T&& value) noexcept(noexcept(std::get_if<I>(std::forward<T>(value))))
	{
		return std::get_if<I>(std::forward<T>(value));
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto HoldsAlternative(TArgs&&... args) noexcept(noexcept(std::holds_alternative<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::holds_alternative<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::holds_alternative<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Visit(TArgs&&... args) noexcept(noexcept(std::visit<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::visit<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::visit<TTypes...>(std::forward<TArgs>(args)...);
	}

	template <typename... TTypes, typename... TArgs>
	constexpr auto Tie(TArgs&&... args) noexcept(noexcept(std::tie<TTypes...>(std::forward<TArgs>(args)...)))
	    -> decltype(std::tie<TTypes...>(std::forward<TArgs>(args)...))
	{
		return std::tie<TTypes...>(std::forward<TArgs>(args)...);
	}
}
