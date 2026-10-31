#pragma once

#include <cmath>
#include <utility>

namespace rox::core
{
	template <typename... TArgs>
	constexpr auto Abs(TArgs&&... args) noexcept(noexcept(std::abs(std::forward<TArgs>(args)...)))
	    -> decltype(std::abs(std::forward<TArgs>(args)...))
	{
		return std::abs(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto IsFinite(TArgs&&... args) noexcept(noexcept(std::isfinite(std::forward<TArgs>(args)...)))
	    -> decltype(std::isfinite(std::forward<TArgs>(args)...))
	{
		return std::isfinite(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto IsNaN(TArgs&&... args) noexcept(noexcept(std::isnan(std::forward<TArgs>(args)...)))
	    -> decltype(std::isnan(std::forward<TArgs>(args)...))
	{
		return std::isnan(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto SignBit(TArgs&&... args) noexcept(noexcept(std::signbit(std::forward<TArgs>(args)...)))
	    -> decltype(std::signbit(std::forward<TArgs>(args)...))
	{
		return std::signbit(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Floor(TArgs&&... args) noexcept(noexcept(std::floor(std::forward<TArgs>(args)...)))
	    -> decltype(std::floor(std::forward<TArgs>(args)...))
	{
		return std::floor(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Ceil(TArgs&&... args) noexcept(noexcept(std::ceil(std::forward<TArgs>(args)...)))
	    -> decltype(std::ceil(std::forward<TArgs>(args)...))
	{
		return std::ceil(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Round(TArgs&&... args) noexcept(noexcept(std::round(std::forward<TArgs>(args)...)))
	    -> decltype(std::round(std::forward<TArgs>(args)...))
	{
		return std::round(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto RoundToLong(TArgs&&... args) noexcept(noexcept(std::lround(std::forward<TArgs>(args)...)))
	    -> decltype(std::lround(std::forward<TArgs>(args)...))
	{
		return std::lround(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto RoundToLongLong(TArgs&&... args) noexcept(noexcept(std::llround(std::forward<TArgs>(args)...)))
	    -> decltype(std::llround(std::forward<TArgs>(args)...))
	{
		return std::llround(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Trunc(TArgs&&... args) noexcept(noexcept(std::trunc(std::forward<TArgs>(args)...)))
	    -> decltype(std::trunc(std::forward<TArgs>(args)...))
	{
		return std::trunc(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Sqrt(TArgs&&... args) noexcept(noexcept(std::sqrt(std::forward<TArgs>(args)...)))
	    -> decltype(std::sqrt(std::forward<TArgs>(args)...))
	{
		return std::sqrt(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Cbrt(TArgs&&... args) noexcept(noexcept(std::cbrt(std::forward<TArgs>(args)...)))
	    -> decltype(std::cbrt(std::forward<TArgs>(args)...))
	{
		return std::cbrt(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Hypot(TArgs&&... args) noexcept(noexcept(std::hypot(std::forward<TArgs>(args)...)))
	    -> decltype(std::hypot(std::forward<TArgs>(args)...))
	{
		return std::hypot(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Cos(TArgs&&... args) noexcept(noexcept(std::cos(std::forward<TArgs>(args)...)))
	    -> decltype(std::cos(std::forward<TArgs>(args)...))
	{
		return std::cos(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Sin(TArgs&&... args) noexcept(noexcept(std::sin(std::forward<TArgs>(args)...)))
	    -> decltype(std::sin(std::forward<TArgs>(args)...))
	{
		return std::sin(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Tan(TArgs&&... args) noexcept(noexcept(std::tan(std::forward<TArgs>(args)...)))
	    -> decltype(std::tan(std::forward<TArgs>(args)...))
	{
		return std::tan(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Acos(TArgs&&... args) noexcept(noexcept(std::acos(std::forward<TArgs>(args)...)))
	    -> decltype(std::acos(std::forward<TArgs>(args)...))
	{
		return std::acos(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Asin(TArgs&&... args) noexcept(noexcept(std::asin(std::forward<TArgs>(args)...)))
	    -> decltype(std::asin(std::forward<TArgs>(args)...))
	{
		return std::asin(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Atan2(TArgs&&... args) noexcept(noexcept(std::atan2(std::forward<TArgs>(args)...)))
	    -> decltype(std::atan2(std::forward<TArgs>(args)...))
	{
		return std::atan2(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Pow(TArgs&&... args) noexcept(noexcept(std::pow(std::forward<TArgs>(args)...)))
	    -> decltype(std::pow(std::forward<TArgs>(args)...))
	{
		return std::pow(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Fma(TArgs&&... args) noexcept(noexcept(std::fma(std::forward<TArgs>(args)...)))
	    -> decltype(std::fma(std::forward<TArgs>(args)...))
	{
		return std::fma(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Exp(TArgs&&... args) noexcept(noexcept(std::exp(std::forward<TArgs>(args)...)))
	    -> decltype(std::exp(std::forward<TArgs>(args)...))
	{
		return std::exp(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Exp2(TArgs&&... args) noexcept(noexcept(std::exp2(std::forward<TArgs>(args)...)))
	    -> decltype(std::exp2(std::forward<TArgs>(args)...))
	{
		return std::exp2(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Expm1(TArgs&&... args) noexcept(noexcept(std::expm1(std::forward<TArgs>(args)...)))
	    -> decltype(std::expm1(std::forward<TArgs>(args)...))
	{
		return std::expm1(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Log1p(TArgs&&... args) noexcept(noexcept(std::log1p(std::forward<TArgs>(args)...)))
	    -> decltype(std::log1p(std::forward<TArgs>(args)...))
	{
		return std::log1p(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Log2(TArgs&&... args) noexcept(noexcept(std::log2(std::forward<TArgs>(args)...)))
	    -> decltype(std::log2(std::forward<TArgs>(args)...))
	{
		return std::log2(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto CopySign(TArgs&&... args) noexcept(noexcept(std::copysign(std::forward<TArgs>(args)...)))
	    -> decltype(std::copysign(std::forward<TArgs>(args)...))
	{
		return std::copysign(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto NextAfter(TArgs&&... args) noexcept(noexcept(std::nextafter(std::forward<TArgs>(args)...)))
	    -> decltype(std::nextafter(std::forward<TArgs>(args)...))
	{
		return std::nextafter(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Fmod(TArgs&&... args) noexcept(noexcept(std::fmod(std::forward<TArgs>(args)...)))
	    -> decltype(std::fmod(std::forward<TArgs>(args)...))
	{
		return std::fmod(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Remainder(TArgs&&... args) noexcept(noexcept(std::remainder(std::forward<TArgs>(args)...)))
	    -> decltype(std::remainder(std::forward<TArgs>(args)...))
	{
		return std::remainder(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto Fabs(TArgs&&... args) noexcept(noexcept(std::fabs(std::forward<TArgs>(args)...)))
	    -> decltype(std::fabs(std::forward<TArgs>(args)...))
	{
		return std::fabs(std::forward<TArgs>(args)...);
	}

	template <typename... TArgs>
	constexpr auto LerpValue(TArgs&&... args) noexcept(noexcept(std::lerp(std::forward<TArgs>(args)...)))
	    -> decltype(std::lerp(std::forward<TArgs>(args)...))
	{
		return std::lerp(std::forward<TArgs>(args)...);
	}

}
