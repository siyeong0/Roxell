#pragma once

#include <Types.h>
#include <TypeTraits.hpp>

namespace rox::math
{
	using core::float32;
	using core::float64;
	using core::int32;
	using core::int64;
	using core::uint32;
	using core::uint64;
	using core::uint8;
	using core::usize;

	// Math arithmetic and results stay in T; choosing double is an explicit caller decision.
	// Complex algorithms are instantiated for these two scalar types in Runtime-Core.
	template <class T>
	concept MathScalar = core::IsSame<T, float32> || core::IsSame<T, float64>;
} // namespace rox::math
