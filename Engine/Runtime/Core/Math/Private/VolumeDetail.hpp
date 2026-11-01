#pragma once

#include <MathScalar.hpp>

#include "GeometryDetail.hpp"
#include <LineSegment.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Quaternion.hpp>

#include <algorithm>
#include <type_traits>

namespace rox::math::detail
{
	template <MathScalar T>
	struct VolumeFrame final
	{
		GeometryVector<T> Axes[3];
	};

	template <MathScalar T>
	inline VolumeFrame<T> VolumeRotation(const Quaternion<T>& rotation) noexcept
	{
		const T magnitude = core::Max({core::Abs(rotation.X), core::Abs(rotation.Y), core::Abs(rotation.Z), core::Abs(rotation.W)});
		const T x = rotation.X / magnitude;
		const T y = rotation.Y / magnitude;
		const T z = rotation.Z / magnitude;
		const T w = rotation.W / magnitude;
		const T scale = T(2.0) / (x * x + y * y + z * z + w * w);

		return {{{T(1.0) - scale * (y * y + z * z), scale * (x * y + w * z), scale * (x * z - w * y)},
		         {scale * (x * y - w * z), T(1.0) - scale * (x * x + z * z), scale * (y * z + w * x)},
		         {scale * (x * z + w * y), scale * (y * z - w * x), T(1.0) - scale * (x * x + y * y)}}};
	}

	template <MathScalar T>
	inline GeometryVector<T> VolumeWorld(const VolumeFrame<T>& frame, const GeometryVector<T>& vector) noexcept
	{
		return frame.Axes[0] * vector.X + frame.Axes[1] * vector.Y + frame.Axes[2] * vector.Z;
	}

	template <MathScalar T>
	inline GeometryVector<T> VolumeLocal(const VolumeFrame<T>& frame, const GeometryVector<T>& vector) noexcept
	{
		return {frame.Axes[0].Dot(vector), frame.Axes[1].Dot(vector), frame.Axes[2].Dot(vector)};
	}

	template <MathScalar T>
	inline T VolumeOutward(T first, T second, bool upper) noexcept
	{
		const T value = first + second;
		T result = GeometryCoordinate<T>(value);
		const T virtualSecond = value - first;
		const T error = (first - (value - virtualSecond)) + (second - virtualSecond);

		if (upper ? error > T(0) : error < T(0))
		{
			result = core::NextAfter(result, upper ? core::NumericLimits<T>::infinity()
			                                            : -core::NumericLimits<T>::infinity());
		}

		return result;
	}

	template <MathScalar T>
	struct VolumeSegmentPairResult final
	{
		GeometryVector<T> First;
		GeometryVector<T> Second;

		T FirstParameter;
		T SecondParameter;
	};

	template <MathScalar T, bool TestRadii = false, MathScalar... RadiusTypes>
	inline std::conditional_t<TestRadii, bool, VolumeSegmentPairResult<T>> VolumeSegmentPair(const LineSegment<T>& first, const LineSegment<T>& second, RadiusTypes... radii) noexcept
	{
		// Keep coordinate-only calls free of unused radius arguments.
		static_assert(sizeof...(radii) == (TestRadii ? 2 : 0));

		const GeometryVector<T> start(first.Start);
		const GeometryVector<T> otherStart(second.Start);

		GeometryVector<T> direction = GeometryVector<T>(first.End) - start;
		GeometryVector<T> otherDirection = GeometryVector<T>(second.End) - otherStart;
		GeometryVector<T> offset = start - otherStart;

		const GeometryVector<T> rawDirection = direction;
		const GeometryVector<T> rawOtherDirection = otherDirection;

		T firstLength = direction.LengthSquared();
		T secondLength = otherDirection.LengthSquared();
		T minimum = offset.LengthSquared();
		const T largestSquared = core::Max({firstLength, secondLength, minimum});

		// Leave headroom for fourth-degree products in either scalar type.
		const bool rescale = !(largestSquared >= T(0x1p-56) && largestSquared <= T(0x1p56));
		if (rescale)
		{
			const T magnitude = core::Max({direction.MaxAbs(), otherDirection.MaxAbs(), offset.MaxAbs()});
			if (magnitude != T(0) && core::IsFinite(magnitude))
			{
				direction = direction / magnitude;
				otherDirection = otherDirection / magnitude;
				offset = offset / magnitude;
			}
			else if (magnitude != T(0))
			{
				const T scale = core::Max({start.MaxAbs(), otherStart.MaxAbs(),
				                          GeometryVector<T>(first.End).MaxAbs(), GeometryVector<T>(second.End).MaxAbs()});
				direction = GeometryVector<T>(first.End) / scale - start / scale;
				otherDirection = GeometryVector<T>(second.End) / scale - otherStart / scale;
				offset = start / scale - otherStart / scale;
			}

			firstLength = direction.LengthSquared();
			secondLength = otherDirection.LengthSquared();
			minimum = offset.LengthSquared();
		}

		const T directionsDot = direction.Dot(otherDirection);
		const T firstOffset = direction.Dot(offset);
		const T secondOffset = otherDirection.Dot(offset);
		T firstParameter = 0;
		T secondParameter = 0;
		const auto consider = [&](T a, T b)
		{
			const T distance = (offset + direction * a - otherDirection * b).LengthSquared();
			if (distance < minimum || (distance == minimum &&
			    (a < firstParameter || (a == firstParameter && b < secondParameter))))
			{
				minimum = distance;
				firstParameter = a;
				secondParameter = b;
			}
		};
		consider(0, secondLength == 0 ? 0 : core::ClampValue(secondOffset / secondLength, T(0), T(1)));
		consider(1, secondLength == 0 ? 0 : core::ClampValue((secondOffset + directionsDot) / secondLength, T(0), T(1)));
		consider(firstLength == 0 ? 0 : core::ClampValue(-firstOffset / firstLength, T(0), T(1)), 0);
		consider(firstLength == 0 ? 0 : core::ClampValue((directionsDot - firstOffset) / firstLength, T(0), T(1)), 1);

		const GeometryVector<T> normal = direction.Cross(otherDirection);
		const T determinant = normal.LengthSquared();
		if (determinant != 0)
		{
			const T a = otherDirection.Cross(offset).Dot(normal) / determinant;
			const T b = direction.Cross(offset).Dot(normal) / determinant;
			if (a >= 0 && a <= 1 && b >= 0 && b <= 1)
			{
				consider(a, b);
			}
		}

		if constexpr (TestRadii)
		{
			if (!rescale)
			{
				const T radius = (radii + ...);
				const T squaredRadius = radius * radius;
				// Preserve coordinate-based evaluation near contact; this does not enlarge either shape.
				const T roundingBand = T(64) * core::NumericLimits<T>::epsilon() * largestSquared;
				if (squaredRadius >= core::NumericLimits<T>::min() && core::Abs(minimum - squaredRadius) > roundingBand)
				{
					return minimum <= squaredRadius;
				}
			}
		}

		const auto interpolate = [](const LineSegment<T>& segment, T parameter)
		{
			return GeometryVector<T>{core::LerpValue(segment.Start.X, segment.End.X, parameter),
			                         core::LerpValue(segment.Start.Y, segment.End.Y, parameter),
			                         core::LerpValue(segment.Start.Z, segment.End.Z, parameter)};
		};
		VolumeSegmentPairResult<T> result{
		    rescale ? interpolate(first, firstParameter) : start + rawDirection * firstParameter,
		    rescale ? interpolate(second, secondParameter) : otherStart + rawOtherDirection * secondParameter,
		    firstParameter, secondParameter};
		if constexpr (TestRadii)
		{
			return GeometryWithinRadii(result.First, result.Second, radii...);
		}
		else
		{
			return result;
		}
	}

	template <MathScalar T>
	inline bool VolumeSegmentsWithinRadii(const LineSegment<T>& first, const LineSegment<T>& second,
	                                      T firstRadius, T secondRadius) noexcept
	{
		return VolumeSegmentPair<T, true>(first, second, firstRadius, secondRadius);
	}

	template <MathScalar T, bool TestRadii>
	inline std::conditional_t<TestRadii, bool, VolumeSegmentPairResult<T>> VolumePointSegmentQuery(const LineSegment<T>& segment, const Vector3<T>& point, T firstRadius = T(0), T secondRadius = T(0)) noexcept
	{
		const GeometryVector<T> start(segment.Start);
		const GeometryVector<T> query(point);

		GeometryVector<T> direction = GeometryVector<T>(segment.End) - start;
		GeometryVector<T> offset = start - query;

		const GeometryVector<T> rawDirection = direction;

		T lengthSquared = direction.LengthSquared();
		T minimum = offset.LengthSquared();
		const T largestSquared = core::Max(lengthSquared, minimum);
		const bool rescale = !(largestSquared >= T(0x1p-56) && largestSquared <= T(0x1p56));
		
		if (rescale)
		{
			const T magnitude = core::Max(direction.MaxAbs(), offset.MaxAbs());
			if (magnitude != T(0) && core::IsFinite(magnitude))
			{
				direction = direction / magnitude;
				offset = offset / magnitude;
			}
			else if (magnitude != T(0))
			{
				const T distanceScale = core::Max({start.MaxAbs(), query.MaxAbs(), GeometryVector<T>(segment.End).MaxAbs()});
				direction = GeometryVector<T>(segment.End) / distanceScale - start / distanceScale;
				offset = start / distanceScale - query / distanceScale;
			}

			lengthSquared = direction.LengthSquared();
			minimum = offset.LengthSquared();
		}

		T parameter = T(0);
		const auto consider = [&](T candidate)
		{
			const T squared = (offset + direction * candidate).LengthSquared();
			if (squared < minimum || (squared == minimum && candidate < parameter))
			{
				minimum = squared;
				parameter = candidate;
			}
		};
		// Retain endpoint candidates and the same minimum-parameter tie rule as segment pairs.
		consider(T(1));
		consider(lengthSquared == T(0) ? T(0) : core::ClampValue(-direction.Dot(offset) / lengthSquared, T(0), T(1)));
		const GeometryVector<T> closest = rescale
		    ? GeometryVector<T>{core::LerpValue(segment.Start.X, segment.End.X, parameter),
		                        core::LerpValue(segment.Start.Y, segment.End.Y, parameter),
		                        core::LerpValue(segment.Start.Z, segment.End.Z, parameter)}
		    : start + rawDirection * parameter;
		if constexpr (TestRadii)
		{
			return GeometryWithinRadii(closest, query, firstRadius, secondRadius);
		}
		else
		{
			return VolumeSegmentPairResult<T>{closest, query, parameter, T(0)};
		}
	}
} // namespace rox::math::detail
