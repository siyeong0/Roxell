#include <Rect.h>
#include <Assertion.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

#include <cmath>
#include <limits>

namespace rox::math
{
	template <MathScalar T>
	bool Rect<T>::IsValid() const noexcept
	{
		return Min.IsFinite() && Max.IsFinite() && Min.X <= Max.X && Min.Y <= Max.Y;
	}

	template <MathScalar T>
	bool Rect<T>::IsEmpty() const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		return Min.X == Max.X || Min.Y == Max.Y;
	}

	template <MathScalar T>
	bool Rect<T>::Contains(const Vector2<T>& point) const noexcept
	{
		ASSERT(IsValid() && point.IsFinite(), "Rectangle inputs must be valid and finite.");
		return point.X >= Min.X && point.X < Max.X && point.Y >= Min.Y && point.Y < Max.Y;
	}

	template <MathScalar T>
	bool Rect<T>::Overlaps(const Rect<T>& other) const noexcept
	{
		ASSERT(IsValid() && other.IsValid(), "Rectangle inputs must be valid and finite.");
		return !IsEmpty() && !other.IsEmpty() && Min.X < other.Max.X && Max.X > other.Min.X && Min.Y < other.Max.Y && Max.Y > other.Min.Y;
	}

	template <MathScalar T>
	Rect<T> Rect<T>::Intersection(const Rect<T>& other) const noexcept
	{
		if (!Overlaps(other))
		{
			return Empty();
		}

		return {Min.Max(other.Min), Max.Min(other.Max)};
	}

	template <MathScalar T>
	Rect<T> Rect<T>::Union(const Rect<T>& other) const noexcept
	{
		ASSERT(IsValid() && other.IsValid(), "Rectangle inputs must be valid and finite.");

		if (IsEmpty())
		{
			return other.IsEmpty() ? Empty() : other;
		}

		if (other.IsEmpty())
		{
			return *this;
		}

		return {Min.Min(other.Min), Max.Max(other.Max)};
	}

	template <MathScalar T>
	Rect<T> Rect<T>::FromPositionSize(const Vector2<T>& position, const Vector2<T>& size) noexcept
	{
		ASSERT(position.IsFinite() && size.IsFinite() && size.X >= 0 && size.Y >= 0, "Rectangle size must be nonnegative and finite.");
		const T maxX = static_cast<T>(position.X) + size.X;
		const T maxY = static_cast<T>(position.Y) + size.Y;
		constexpr T MAXIMUM = core::NumericLimits<T>::max();
		ASSERT(core::Abs(maxX) <= MAXIMUM && core::Abs(maxY) <= MAXIMUM, "Rectangle result must be representable in the finite scalar range.");
		return {position, {static_cast<T>(maxX), static_cast<T>(maxY)}};
	}

	template <MathScalar T>
	Rect<T> Rect<T>::Translated(const Vector2<T>& offset) const noexcept
	{
		ASSERT(IsValid() && offset.IsFinite(), "Rectangle inputs must be valid and finite.");
		const T minX = static_cast<T>(Min.X) + offset.X;
		const T minY = static_cast<T>(Min.Y) + offset.Y;
		const T maxX = static_cast<T>(Max.X) + offset.X;
		const T maxY = static_cast<T>(Max.Y) + offset.Y;
		constexpr T MAXIMUM = core::NumericLimits<T>::max();
		ASSERT(core::Abs(minX) <= MAXIMUM && core::Abs(minY) <= MAXIMUM && core::Abs(maxX) <= MAXIMUM && core::Abs(maxY) <= MAXIMUM, "Rectangle result must be representable in the finite scalar range.");
		return {{static_cast<T>(minX), static_cast<T>(minY)}, {static_cast<T>(maxX), static_cast<T>(maxY)}};
	}

	template <MathScalar T>
	Vector2<T> Rect<T>::Size() const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		const T width = static_cast<T>(Max.X) - Min.X;
		const T height = static_cast<T>(Max.Y) - Min.Y;
		constexpr T MAXIMUM = core::NumericLimits<T>::max();
		ASSERT(width <= MAXIMUM && height <= MAXIMUM, "Rectangle result must be representable in the finite scalar range.");
		return {static_cast<T>(width), static_cast<T>(height)};
	}

	template <MathScalar T>
	Vector2<T> Rect<T>::Center() const noexcept
	{
		ASSERT(IsValid(), "Bounds must be valid before calculating the center.");
		return Vector2<T>::Lerp(Min, Max, T(0.5));
	}

	template <MathScalar T>
	T Rect<T>::Area() const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		const T width = static_cast<T>(Max.X) - Min.X;
		const T height = static_cast<T>(Max.Y) - Min.Y;
		return width * height;
	}

	template struct Rect<float32>;
	template struct Rect<float64>;
} // namespace rox::math
