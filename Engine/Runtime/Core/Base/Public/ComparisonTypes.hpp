#pragma once

#include <compare>
#include <functional>

namespace rox::core
{
	using StrongOrdering = std::strong_ordering;

	template <typename T = void> using Less = std::less<T>;
}
