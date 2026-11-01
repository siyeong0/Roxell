#include <Sphere.h>
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
		[[nodiscard]] T distance(const Vector3<T>& left, const Vector3<T>& right) noexcept
		{
			return core::Hypot(static_cast<T>(left.X) - right.X, static_cast<T>(left.Y) - right.Y, static_cast<T>(left.Z) - right.Z);
		}

		template <MathScalar T>
		[[nodiscard]] T distanceSquared(const Vector3<T>& left, const Vector3<T>& right) noexcept
		{
			const T x = static_cast<T>(left.X) - right.X;
			const T y = static_cast<T>(left.Y) - right.Y;
			const T z = static_cast<T>(left.Z) - right.Z;
			return x * x + y * y + z * z;
		}

		template <MathScalar T>
		[[nodiscard]] bool toFiniteScalar(T value, T* outValue) noexcept
		{
			ASSERT(outValue != nullptr, "Converted float output must not be null.");

			if (!core::IsFinite(value) || core::Abs(value) > static_cast<T>(core::NumericLimits<T>::max()))
			{
				return false;
			}

			*outValue = static_cast<T>(value);
			return true;
		}

		template <MathScalar T>
		[[nodiscard]] bool tryRoundRadiusUp(T value, T* outRadius) noexcept
		{
			ASSERT(outRadius != nullptr, "Rounded sphere radius output must not be null.");

			if (!core::IsFinite(value) || value < T(0.0) || value > static_cast<T>(core::NumericLimits<T>::max()))
			{
				return false;
			}

			T radius = static_cast<T>(value);

			if (static_cast<T>(radius) < value)
			{
				radius = core::NextAfter(radius, core::NumericLimits<T>::infinity());
			}

			if (!core::IsFinite(radius))
			{
				return false;
			}

			*outRadius = radius;
			return true;
		}
	} // namespace

	template <MathScalar T>
	Sphere<T>::Sphere(const Vector3<T>& center, T radius) noexcept : Center(center), Radius(radius)
	{
		ASSERT(center.IsFinite(), "Sphere center must be finite.");
		ASSERT(core::IsFinite(radius) && radius >= T(0.0), "Sphere radius must be finite and non-negative.");
	}

	template <MathScalar T>
	bool Sphere<T>::IsValid() const noexcept
	{
		return Center.IsFinite() && core::IsFinite(Radius) && Radius >= T(0.0);
	}

	template <MathScalar T>
	bool Sphere<T>::Contains(const Vector3<T>& point) const noexcept
	{
		return IsValid() && point.IsFinite() && distance<T>(point, Center) <= Radius;
	}

	template <MathScalar T>
	bool Sphere<T>::Overlaps(const Sphere<T>& other) const noexcept
	{
		if (!IsValid() || !other.IsValid())
		{
			return false;
		}

		const T combinedRadius = static_cast<T>(Radius) + static_cast<T>(other.Radius);
		if (core::IsFinite(combinedRadius))
		{
			return distance<T>(other.Center, Center) <= combinedRadius;
		}

		return distance<T>(other.Center * T(0.5), Center * T(0.5)) <= Radius * T(0.5) + other.Radius * T(0.5);
	}

	template <MathScalar T>
	bool Sphere<T>::TryEncapsulate(const Vector3<T>& point) noexcept
	{
		if (!IsValid() || !point.IsFinite())
		{
			return false;
		}

		const T xOffset = static_cast<T>(point.X) - static_cast<T>(Center.X);
		const T yOffset = static_cast<T>(point.Y) - static_cast<T>(Center.Y);
		const T zOffset = static_cast<T>(point.Z) - static_cast<T>(Center.Z);
		const T pointDistance = core::Hypot(xOffset, yOffset, zOffset);

		if (pointDistance <= static_cast<T>(Radius))
		{
			return true;
		}

		const T newRadius = Radius * T(0.5) + pointDistance * T(0.5);
		const T centerMove = (newRadius - static_cast<T>(Radius)) / pointDistance;
		Vector3<T> newCenter;
		T newRadiusValue = T(0.0);

		if (!toFiniteScalar<T>(static_cast<T>(Center.X) + xOffset * centerMove, &newCenter.X) ||
		    !toFiniteScalar<T>(static_cast<T>(Center.Y) + yOffset * centerMove, &newCenter.Y) ||
		    !toFiniteScalar<T>(static_cast<T>(Center.Z) + zOffset * centerMove, &newCenter.Z))
		{
			return false;
		}

		const T requiredRadius = core::Max(distance<T>(newCenter, point), distance<T>(newCenter, Center) + static_cast<T>(Radius));

		if (!tryRoundRadiusUp<T>(requiredRadius, &newRadiusValue))
		{
			return false;
		}

		Center = newCenter;
		Radius = newRadiusValue;
		return true;
	}

	template <MathScalar T>
	void Sphere<T>::Encapsulate(const Vector3<T>& point) noexcept
	{
		const bool bEncapsulated = TryEncapsulate(point);
		ASSERT(bEncapsulated, "Sphere and point must be valid and the result must fit in the scalar type.");
	}

	template struct Sphere<float32>;
	template struct Sphere<float64>;
} // namespace rox::math
