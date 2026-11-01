#include <RectInt.h>
#include <Assertion.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

#include <cmath>
#include <limits>

namespace rox::math
{
	bool RectInt::IsValid() const noexcept
	{
		return Min.X <= Max.X && Min.Y <= Max.Y;
	}

	bool RectInt::IsEmpty() const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		return Min.X == Max.X || Min.Y == Max.Y;
	}

	bool RectInt::Contains(const Int2& point) const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		return point.X >= Min.X && point.X < Max.X && point.Y >= Min.Y && point.Y < Max.Y;
	}

	bool RectInt::Overlaps(const RectInt& other) const noexcept
	{
		ASSERT(IsValid() && other.IsValid(), "Rectangle inputs must be valid and finite.");
		return !IsEmpty() && !other.IsEmpty() && Min.X < other.Max.X && Max.X > other.Min.X && Min.Y < other.Max.Y && Max.Y > other.Min.Y;
	}

	RectInt RectInt::Intersection(const RectInt& other) const noexcept
	{
		if (!Overlaps(other))
		{
			return Empty();
		}

		return {Min.Max(other.Min), Max.Min(other.Max)};
	}

	RectInt RectInt::Union(const RectInt& other) const noexcept
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

	RectInt RectInt::FromPositionSize(const Int2& position, const uint32 width, const uint32 height) noexcept
	{
		const int64 maxX = static_cast<int64>(position.X) + width;
		const int64 maxY = static_cast<int64>(position.Y) + height;
		constexpr int64 MAXIMUM = core::NumericLimits<int32>::max();
		ASSERT(maxX <= MAXIMUM && maxY <= MAXIMUM, "Rectangle integer bounds must be representable as int32.");
		return {position, {static_cast<int32>(maxX), static_cast<int32>(maxY)}};
	}

	RectInt RectInt::Translated(const Int2& offset) const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		const int64 minX = static_cast<int64>(Min.X) + offset.X;
		const int64 minY = static_cast<int64>(Min.Y) + offset.Y;
		const int64 maxX = static_cast<int64>(Max.X) + offset.X;
		const int64 maxY = static_cast<int64>(Max.Y) + offset.Y;
		constexpr int64 MINIMUM = core::NumericLimits<int32>::min();
		constexpr int64 MAXIMUM = core::NumericLimits<int32>::max();
		ASSERT(minX >= MINIMUM && minX <= MAXIMUM && minY >= MINIMUM && minY <= MAXIMUM && maxX >= MINIMUM && maxX <= MAXIMUM && maxY >= MINIMUM && maxY <= MAXIMUM, "Rectangle integer bounds must be representable as int32.");
		return {{static_cast<int32>(minX), static_cast<int32>(minY)},
		        {static_cast<int32>(maxX), static_cast<int32>(maxY)}};
	}

	uint32 RectInt::Width() const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		return static_cast<uint32>(static_cast<int64>(Max.X) - Min.X);
	}

	uint32 RectInt::Height() const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		return static_cast<uint32>(static_cast<int64>(Max.Y) - Min.Y);
	}

	uint64 RectInt::Area() const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		return static_cast<uint64>(Width()) * Height();
	}

	template <MathScalar T>
	Vector2<T> RectInt::Center() const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		return {static_cast<T>(static_cast<int64>(Min.X) + Max.X) * T(0.5),
		        static_cast<T>(static_cast<int64>(Min.Y) + Max.Y) * T(0.5)};
	}

	template <MathScalar T>
	Rect<T> RectInt::ToRect() const noexcept
	{
		ASSERT(IsValid(), "Rectangle inputs must be valid and finite.");
		return {Min.ToVector2<T>(), Max.ToVector2<T>()};
	}

	template <MathScalar T>
	RectInt RectInt::Covering(const Rect<T>& rectangle) noexcept
	{
		ASSERT(rectangle.IsValid(), "Rectangle inputs must be valid and finite.");

		if (rectangle.IsEmpty())
		{
			return Empty();
		}

		const T minX = core::Floor(static_cast<T>(rectangle.Min.X));
		const T minY = core::Floor(static_cast<T>(rectangle.Min.Y));
		const T maxX = core::Ceil(static_cast<T>(rectangle.Max.X));
		const T maxY = core::Ceil(static_cast<T>(rectangle.Max.Y));
		constexpr T MINIMUM = T(-2147483648.0);
		constexpr T MAXIMUM = T(2147483648.0);
		ASSERT(minX >= MINIMUM && minX < MAXIMUM && minY >= MINIMUM && minY < MAXIMUM && maxX >= MINIMUM && maxX < MAXIMUM && maxY >= MINIMUM && maxY < MAXIMUM, "Rectangle integer bounds must be representable as int32.");
		return {{static_cast<int32>(minX), static_cast<int32>(minY)},
		        {static_cast<int32>(maxX), static_cast<int32>(maxY)}};
	}

	template Vector2<float32> RectInt::Center<float32>() const noexcept;
	template Rect<float32> RectInt::ToRect<float32>() const noexcept;
	template RectInt RectInt::Covering(const Rect<float32>&) noexcept;
	template Vector2<float64> RectInt::Center<float64>() const noexcept;
	template Rect<float64> RectInt::ToRect<float64>() const noexcept;
	template RectInt RectInt::Covering(const Rect<float64>&) noexcept;
} // namespace rox::math
