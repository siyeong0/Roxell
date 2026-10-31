#include <LineSegment.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	template <MathScalar T>
	static T geometrySegmentParameter(const LineSegment<T>& segment, const Vector3<T>& point) noexcept
	{
		const Vector3<T> a = segment.Start.Abs();
		const Vector3<T> b = segment.End.Abs();
		const T magnitude = rox::core::Max({a.X, a.Y, a.Z, b.X, b.Y, b.Z});
		int exponent = 0;
		std::frexp(magnitude, &exponent);
		const T scale = std::scalbn(T(1), exponent - 1);
		if (magnitude == T(0))
		{
			return T(0);
		}

		const Vector3<T> start = segment.Start / scale;
		const Vector3<T> direction = segment.End / scale - start;
		const T squared = direction.LengthSquared();
		if (squared == T(0))
		{
			return T(0);
		}

		return rox::core::ClampValue((point / scale - start).Dot(direction) / squared, T(0), T(1));
	}

	template <MathScalar T>
	static T geometrySegmentMagnitude(T value) noexcept
	{
		return value > rox::core::NumericLimits<T>::max() ? rox::core::NumericLimits<T>::infinity()
		                                                  : static_cast<T>(value);
	}

	template <MathScalar T>
	static Vector3<T> geometrySegmentPoint(const LineSegment<T>& segment, T parameter) noexcept
	{
		return Vector3<T>::Lerp(segment.Start, segment.End, parameter);
	}

	template <MathScalar T>
	bool LineSegment<T>::IsValid() const noexcept
	{
		return Start.IsFinite() && End.IsFinite();
	}

	template <MathScalar T>
	Vector3<T> LineSegment<T>::At(T parameter) const noexcept
	{
		ASSERT(IsValid(), "Segment endpoints must be finite.");
		ASSERT(rox::core::IsFinite(parameter) && parameter >= 0 && parameter <= 1, "Segment parameter must be finite and within zero and one.");
		return geometrySegmentPoint<T>(*this, parameter);
	}

	template <MathScalar T>
	T LineSegment<T>::Length() const noexcept
	{
		ASSERT(IsValid(), "Segment endpoints must be finite.");
		return geometrySegmentMagnitude<T>(rox::core::Hypot(static_cast<T>(End.X) - Start.X, static_cast<T>(End.Y) - Start.Y, static_cast<T>(End.Z) - Start.Z));
	}

	template <MathScalar T>
	T LineSegment<T>::LengthSquared() const noexcept
	{
		ASSERT(IsValid(), "Segment endpoints must be finite.");
		const T x = static_cast<T>(End.X) - Start.X;
		const T y = static_cast<T>(End.Y) - Start.Y;
		const T z = static_cast<T>(End.Z) - Start.Z;
		return geometrySegmentMagnitude<T>(x * x + y * y + z * z);
	}

	template <MathScalar T>
	T LineSegment<T>::ClosestParameter(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Segment endpoints must be finite.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		return static_cast<T>(geometrySegmentParameter<T>(*this, point));
	}

	template <MathScalar T>
	Vector3<T> LineSegment<T>::ClosestPoint(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Segment endpoints must be finite.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		return geometrySegmentPoint<T>(*this, geometrySegmentParameter<T>(*this, point));
	}

	template <MathScalar T>
	T LineSegment<T>::DistanceSquared(const Vector3<T>& point) const noexcept
	{
		ASSERT(IsValid(), "Segment endpoints must be finite.");
		ASSERT(point.IsFinite(), "Geometry point must be finite.");
		const T parameter = geometrySegmentParameter<T>(*this, point);
		const T x = (static_cast<T>(point.X) - Start.X) - parameter * (static_cast<T>(End.X) - Start.X);
		const T y = (static_cast<T>(point.Y) - Start.Y) - parameter * (static_cast<T>(End.Y) - Start.Y);
		const T z = (static_cast<T>(point.Z) - Start.Z) - parameter * (static_cast<T>(End.Z) - Start.Z);
		return geometrySegmentMagnitude<T>(x * x + y * y + z * z);
	}

	template <MathScalar T>
	Box<T> LineSegment<T>::GetBounds() const noexcept
	{
		ASSERT(IsValid(), "Segment endpoints must be finite.");
		return {{rox::core::Min(Start.X, End.X), rox::core::Min(Start.Y, End.Y), rox::core::Min(Start.Z, End.Z)},
		        {rox::core::Max(Start.X, End.X), rox::core::Max(Start.Y, End.Y), rox::core::Max(Start.Z, End.Z)}};
	}

	template struct LineSegment<float32>;
	template struct LineSegment<float64>;
} // namespace rox::math
