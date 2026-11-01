#pragma once

#include <MathScalar.hpp>

#include <Int2.hpp>
#include <Rect.h>
#include <TypeTraits.hpp>

#include <cstddef>
#include <type_traits>

namespace rox::math
{
	struct RectInt final
	{
		Int2 Min = Int2::Zero();
		Int2 Max = Int2::Zero();

		constexpr RectInt() noexcept = default;

		constexpr RectInt(const Int2& min, const Int2& max) noexcept : Min(min), Max(max)
		{
		}

		[[nodiscard]] static constexpr RectInt Empty() noexcept
		{
			return {};
		}

		[[nodiscard]] constexpr bool operator==(const RectInt&) const noexcept = default;
		[[nodiscard]] bool IsValid() const noexcept;

		// Operations require ordered bounds and use inclusive minimum, exclusive maximum bounds.
		[[nodiscard]] bool IsEmpty() const noexcept;
		[[nodiscard]] bool Contains(const Int2& point) const noexcept;
		[[nodiscard]] bool Overlaps(const RectInt& other) const noexcept;
		[[nodiscard]] RectInt Intersection(const RectInt& other) const noexcept;
		[[nodiscard]] RectInt Union(const RectInt& other) const noexcept;

		[[nodiscard]] static RectInt FromPositionSize(const Int2& position, uint32 width, uint32 height) noexcept;
		[[nodiscard]] RectInt Translated(const Int2& offset) const noexcept;
		[[nodiscard]] uint32 Width() const noexcept;
		[[nodiscard]] uint32 Height() const noexcept;
		[[nodiscard]] uint64 Area() const noexcept;

		template <MathScalar T = float32>
		[[nodiscard]] Vector2<T> Center() const noexcept;

		// Float32 rounding can collapse narrow rectangles at large coordinates.
		template <MathScalar T = float32>
		[[nodiscard]] Rect<T> ToRect() const noexcept;

		// Empty input returns canonical Empty; nonempty covering bounds must fit int32.
		template <MathScalar T>
		[[nodiscard]] static RectInt Covering(const Rect<T>& rectangle) noexcept;
	};

	static_assert(sizeof(RectInt) == 4 * sizeof(int32) && alignof(RectInt) == alignof(int32));
	static_assert(offsetof(RectInt, Min) == 0 && offsetof(RectInt, Max) == 2 * sizeof(int32));
	static_assert(core::IsStandardLayout<RectInt> && core::IsTriviallyCopyable<RectInt>);
} // namespace rox::math
