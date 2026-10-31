#pragma once

#include <type_traits>
#include <concepts>
#include <functional>

namespace rox::core
{
	template <typename T> using AddPointer = std::add_pointer_t<T>;

	template <typename... T> inline constexpr bool IsArithmetic = std::is_arithmetic_v<T...>;
	template <typename... T> inline constexpr bool IsArray = std::is_array_v<T...>;
	template <typename... T> inline constexpr bool IsBaseOf = std::is_base_of_v<T...>;
	template <typename... T> inline constexpr bool IsConst = std::is_const_v<T...>;
	template <typename... T> inline constexpr bool IsConstructible = std::is_constructible_v<T...>;
	template <typename... T> inline constexpr bool IsConvertible = std::is_convertible_v<T...>;
	template <typename... T> inline constexpr bool IsCopyConstructible = std::is_copy_constructible_v<T...>;
	template <typename... T> inline constexpr bool IsEnum = std::is_enum_v<T...>;
	template <typename... T> inline constexpr bool IsFloatingPoint = std::is_floating_point_v<T...>;
	template <typename... T> inline constexpr bool IsFunction = std::is_function_v<T...>;
	template <typename... T> inline constexpr bool IsIntegral = std::is_integral_v<T...>;
	template <typename... T> inline constexpr bool IsLvalueReference = std::is_lvalue_reference_v<T...>;
	template <typename... T> inline constexpr bool IsMemberPointer = std::is_member_pointer_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowConstructible = std::is_nothrow_constructible_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowConvertible = std::is_nothrow_convertible_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowCopyAssignable = std::is_nothrow_copy_assignable_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowCopyConstructible = std::is_nothrow_copy_constructible_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowDefaultConstructible = std::is_nothrow_default_constructible_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowDestructible = std::is_nothrow_destructible_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowInvocableR = std::is_nothrow_invocable_r_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowInvocable = std::is_nothrow_invocable_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowMoveAssignable = std::is_nothrow_move_assignable_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowMoveConstructible = std::is_nothrow_move_constructible_v<T...>;
	template <typename... T> inline constexpr bool IsNothrowSwappable = std::is_nothrow_swappable_v<T...>;
	template <typename... T> inline constexpr bool IsObject = std::is_object_v<T...>;
	template <typename... T> inline constexpr bool IsPointer = std::is_pointer_v<T...>;
	template <typename... T> inline constexpr bool IsReference = std::is_reference_v<T...>;
	template <typename... T> inline constexpr bool IsSame = std::is_same_v<T...>;
	template <typename... T> inline constexpr bool IsSigned = std::is_signed_v<T...>;
	template <typename... T> inline constexpr bool IsStandardLayout = std::is_standard_layout_v<T...>;
	template <typename... T> inline constexpr bool IsTrivial = std::is_trivial_v<T...>;
	template <typename... T> inline constexpr bool IsTriviallyCopyable = std::is_trivially_copyable_v<T...>;
	template <typename... T> inline constexpr bool IsTriviallyDestructible = std::is_trivially_destructible_v<T...>;
	template <typename... T> inline constexpr bool IsVoid = std::is_void_v<T...>;
	template <typename... T> inline constexpr bool IsVolatile = std::is_volatile_v<T...>;

	template <typename T> using MakeUnsigned = std::make_unsigned_t<T>;
	template <typename T> using RemoveAllExtents = std::remove_all_extents_t<T>;
	template <typename T> using RemoveConst = std::remove_const_t<T>;
	template <typename T> using RemoveCv = std::remove_cv_t<T>;
	template <typename T> using RemoveCvref = std::remove_cvref_t<T>;
	template <typename T> using RemoveExtent = std::remove_extent_t<T>;
	template <typename T> using RemovePointer = std::remove_pointer_t<T>;
	template <typename T> using RemoveReference = std::remove_reference_t<T>;
	template <typename T> using UnderlyingType = std::underlying_type_t<T>;

	template <typename T, typename U> concept SameAs = std::same_as<T, U>;
	template <typename T, typename U> concept DerivedFrom = std::derived_from<T, U>;
	template <typename T, typename U> concept ConvertibleTo = std::convertible_to<T, U>;
}
