#include <Box.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		[[nodiscard]] T finiteScalar(T value) noexcept
		{
			ASSERT(rox::core::IsFinite(value), "Box calculation must produce a finite value.");
			ASSERT(rox::core::Abs(value) <= static_cast<T>(rox::core::NumericLimits<T>::max()), "Box calculation must fit in the scalar type.");
			return static_cast<T>(value);
		}

		template <MathScalar T>
		[[nodiscard]] T toNonNegativeFloatOrInfinity(T value) noexcept
		{
			ASSERT(value >= T(0.0), "Box magnitude must be finite and non-negative.");

			if (value > static_cast<T>(rox::core::NumericLimits<T>::max()))
			{
				return rox::core::NumericLimits<T>::infinity();
			}

			return static_cast<T>(value);
		}
	} // namespace

	template <MathScalar T>
	bool Box<T>::IsValid() const noexcept
	{
		return Min.IsFinite() && Max.IsFinite() && Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z;
	}

	template <MathScalar T>
	Vector3<T> Box<T>::Center() const noexcept
	{
		ASSERT(IsValid(), "Bounds must be valid before calculating the center.");
		return Vector3<T>::Lerp(Min, Max, T(0.5));
	}

	template <MathScalar T>
	Vector3<T> Box<T>::Size() const noexcept
	{
		ASSERT(IsValid(), "Box must be valid before calculating its size.");
		return {toNonNegativeFloatOrInfinity<T>(static_cast<T>(Max.X) - static_cast<T>(Min.X)),
		        toNonNegativeFloatOrInfinity<T>(static_cast<T>(Max.Y) - static_cast<T>(Min.Y)),
		        toNonNegativeFloatOrInfinity<T>(static_cast<T>(Max.Z) - static_cast<T>(Min.Z))};
	}

	template <MathScalar T>
	Vector3<T> Box<T>::Extents() const noexcept
	{
		ASSERT(IsValid(), "Box must be valid before calculating its extents.");
		return Max * T(0.5) - Min * T(0.5);
	}

	template <MathScalar T>
	T Box<T>::Volume() const noexcept
	{
		ASSERT(IsValid(), "Box must be valid before calculating its volume.");
		const T width = static_cast<T>(Max.X) - static_cast<T>(Min.X);
		const T height = static_cast<T>(Max.Y) - static_cast<T>(Min.Y);
		const T depth = static_cast<T>(Max.Z) - static_cast<T>(Min.Z);

		if (width == T(0.0) || height == T(0.0) || depth == T(0.0))
		{
			return T(0.0);
		}

		return toNonNegativeFloatOrInfinity<T>(width * height * depth);
	}

	template <MathScalar T>
	void Box<T>::Encapsulate(const Vector3<T>& point) noexcept
	{
		ASSERT(point.IsFinite(), "Box can encapsulate only a finite point.");
		Min.X = rox::core::Min(Min.X, point.X);
		Min.Y = rox::core::Min(Min.Y, point.Y);
		Min.Z = rox::core::Min(Min.Z, point.Z);
		Max.X = rox::core::Max(Max.X, point.X);
		Max.Y = rox::core::Max(Max.Y, point.Y);
		Max.Z = rox::core::Max(Max.Z, point.Z);
	}

	template <MathScalar T>
	void Box<T>::Encapsulate(const Box<T>& other) noexcept
	{
		if (!other.IsValid())
		{
			return;
		}

		Encapsulate(other.Min);
		Encapsulate(other.Max);
	}

	template <MathScalar T>
	bool Box<T>::Contains(const Vector3<T>& point) const noexcept
	{
		return IsValid() && point.IsFinite() && point.X >= Min.X && point.X <= Max.X && point.Y >= Min.Y && point.Y <= Max.Y && point.Z >= Min.Z && point.Z <= Max.Z;
	}

	template <MathScalar T>
	bool Box<T>::Overlaps(const Box<T>& other) const noexcept
	{
		return IsValid() && other.IsValid() && Min.X <= other.Max.X && Max.X >= other.Min.X && Min.Y <= other.Max.Y && Max.Y >= other.Min.Y && Min.Z <= other.Max.Z && Max.Z >= other.Min.Z;
	}

	template struct Box<float32>;
	template struct Box<float64>;
} // namespace rox::math
