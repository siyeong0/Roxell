#pragma once

#include <algorithm>
#include <initializer_list>

namespace rox::core
{
	template <typename T> constexpr const T& Min(const T& left, const T& right) noexcept(noexcept(std::min(left, right)))
	{
		return std::min(left, right);
	}

	template <typename T> constexpr T Min(std::initializer_list<T> values)
	{
		return std::min(values);
	}

	template <typename T> constexpr const T& Max(const T& left, const T& right) noexcept(noexcept(std::max(left, right)))
	{
		return std::max(left, right);
	}

	template <typename T> constexpr T Max(std::initializer_list<T> values)
	{
		return std::max(values);
	}

	template <typename T> constexpr const T& ClampValue(const T& value, const T& minimum, const T& maximum)
	{
		return std::clamp(value, minimum, maximum);
	}
}
