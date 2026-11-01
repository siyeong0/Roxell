#pragma once

#include <MathScalar.hpp>

#include <TypeTraits.hpp>
#include <Vector2.hpp>

#include <cstddef>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T = float32>
	struct Rect final
	{
		Vector2<T> Min = Vector2<T>::Zero();
		Vector2<T> Max = Vector2<T>::Zero();

		constexpr Rect() noexcept = default;

		constexpr Rect(const Vector2<T>& min, const Vector2<T>& max) noexcept 
			: Min(min)
			, Max(max)
		{
		}

		[[nodiscard]] static constexpr Rect<T> Empty() noexcept
		{
			return {};
		}

		[[nodiscard]] constexpr bool operator==(const Rect<T>&) const noexcept = default;
		[[nodiscard]] bool IsValid() const noexcept;

		// Operations require valid inputs and use inclusive minimum, exclusive maximum bounds.
		[[nodiscard]] bool IsEmpty() const noexcept;
		[[nodiscard]] bool Contains(const Vector2<T>& point) const noexcept;
		[[nodiscard]] bool Overlaps(const Rect<T>& other) const noexcept;
		[[nodiscard]] Rect<T> Intersection(const Rect<T>& other) const noexcept;
		[[nodiscard]] Rect<T> Union(const Rect<T>& other) const noexcept;

		[[nodiscard]] static Rect<T> FromPositionSize(const Vector2<T>& position, const Vector2<T>& size) noexcept;
		[[nodiscard]] Rect<T> Translated(const Vector2<T>& offset) const noexcept;
		[[nodiscard]] Vector2<T> Size() const noexcept;
		[[nodiscard]] Vector2<T> Center() const noexcept;
		[[nodiscard]] T Area() const noexcept;

		template <MathScalar U>
		explicit constexpr Rect(const Rect<U>& other) noexcept : Min(other.Min), Max(other.Max)
		{
		}
	};

	static_assert(sizeof(Rect<float32>) == 4 * sizeof(float32) && alignof(Rect<float32>) == alignof(float32));
	static_assert(sizeof(Rect<float64>) == 4 * sizeof(float64) && alignof(Rect<float64>) == alignof(float64));
	static_assert(offsetof(Rect<float32>, Min) == 0 && offsetof(Rect<float32>, Max) == 2 * sizeof(float32));
	static_assert(offsetof(Rect<float64>, Min) == 0 && offsetof(Rect<float64>, Max) == 2 * sizeof(float64));
	static_assert(core::IsStandardLayout<Rect<float32>> && core::IsTriviallyCopyable<Rect<float32>>);
	static_assert(core::IsStandardLayout<Rect<float64>> && core::IsTriviallyCopyable<Rect<float64>>);
} // namespace rox::math
