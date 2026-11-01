#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Vector3.hpp>

#include <cmath>
#include <limits>

namespace rox::math::detail
{
	template <MathScalar T>
	struct GeometryVector final
	{
		T X;
		T Y;
		T Z;

		GeometryVector(T x, T y, T z) noexcept : X(x), Y(y), Z(z)
		{
		}

		explicit GeometryVector(const Vector3<T>& value) noexcept : X(value.X), Y(value.Y), Z(value.Z)
		{
		}

		GeometryVector<T> operator+(const GeometryVector<T>& other) const noexcept
		{
			return {X + other.X, Y + other.Y, Z + other.Z};
		}

		GeometryVector<T> operator-(const GeometryVector<T>& other) const noexcept
		{
			return {X - other.X, Y - other.Y, Z - other.Z};
		}

		GeometryVector<T> operator*(T scale) const noexcept
		{
			return {X * scale, Y * scale, Z * scale};
		}

		T Dot(const GeometryVector<T>& other) const noexcept
		{
			return X * other.X + Y * other.Y + Z * other.Z;
		}

		GeometryVector<T> Cross(const GeometryVector<T>& other) const noexcept
		{
			return {Y * other.Z - Z * other.Y, Z * other.X - X * other.Z, X * other.Y - Y * other.X};
		}

		T LengthSquared() const noexcept
		{
			return Dot(*this);
		}

		T Length() const noexcept
		{
			return core::Hypot(X, Y, Z);
		}

		T MaxAbs() const noexcept
		{
			return core::Max({core::Abs(X), core::Abs(Y), core::Abs(Z)});
		}

		GeometryVector<T> operator/(T scale) const noexcept
		{
			return {X / scale, Y / scale, Z / scale};
		}
	};

	template <MathScalar T>
	inline bool GeometryWithinRadiiScaled(const GeometryVector<T>& first, const GeometryVector<T>& second, T firstRadius, T secondRadius) noexcept
	{
		GeometryVector<T> offset = first - second;
		T scale = core::Max({offset.MaxAbs(), firstRadius, secondRadius});
		if (scale == T(0))
		{
			return true;
		}

		if (!core::IsFinite(scale))
		{
			offset = first * T(0.5) - second * T(0.5);
			firstRadius *= T(0.5);
			secondRadius *= T(0.5);
			scale = core::Max({offset.MaxAbs(), firstRadius, secondRadius});
		}

		offset = offset / scale;
		const T radius = firstRadius / scale + secondRadius / scale;
		return offset.LengthSquared() <= radius * radius;
	}

	template <MathScalar T>
	inline bool GeometryWithinRadii(const GeometryVector<T>& first, const GeometryVector<T>& second, T firstRadius, T secondRadius = T(0)) noexcept
	{
		const T radius = firstRadius + secondRadius;
		const T squared = radius * radius;
		if (squared >= core::NumericLimits<T>::min() && squared <= core::NumericLimits<T>::max())
		{
			return (first - second).LengthSquared() <= squared;
		}

		return GeometryWithinRadiiScaled(first, second, firstRadius, secondRadius);
	}

	template <MathScalar T>
	inline T GeometryCoordinate(T value) noexcept
	{
		ASSERT(core::IsFinite(value), "Geometry coordinate must fit the finite scalar range.");
		return static_cast<T>(value);
	}

	template <MathScalar T>
	inline Vector3<T> GeometryPoint(const GeometryVector<T>& value) noexcept
	{
		return {GeometryCoordinate<T>(value.X), GeometryCoordinate<T>(value.Y), GeometryCoordinate<T>(value.Z)};
	}

	template <MathScalar T>
	inline T GeometryMagnitude(T value) noexcept
	{
		return value > core::NumericLimits<T>::max() ? core::NumericLimits<T>::infinity()
		                                                  : static_cast<T>(value);
	}
} // namespace rox::math::detail
