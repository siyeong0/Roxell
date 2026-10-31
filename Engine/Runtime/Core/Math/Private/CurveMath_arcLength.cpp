#include <CurveMath.h>
#include "CurveDetail.hpp"
#include <Numeric.hpp>
#include <NumericLimits.hpp>

namespace rox::math
{
	template <MathScalar T>
	static T curveLength(detail::ECurveKind kind, const detail::CurveSample<T>* controls, uint32 subdivisions) noexcept
	{
		detail::CurveSample<T> previous = detail::CurveEvaluate<T>(kind, controls, 0, false);
		T length = 0;

		for (uint64 index = 1; index <= subdivisions; ++index)
		{
			const detail::CurveSample<T> current = detail::CurveEvaluate<T>(kind, controls, static_cast<T>(index) / subdivisions, false);
			const T x = current.Components[0] - previous.Components[0];
			const T y = current.Components[1] - previous.Components[1];
			const T z = current.Components[2] - previous.Components[2];
			const T w = current.Components[3] - previous.Components[3];
			length += rox::core::Hypot(rox::core::Hypot(x, y), rox::core::Hypot(z, w));
			previous = current;
		}

		return length > rox::core::NumericLimits<T>::max() ? rox::core::NumericLimits<T>::infinity() : static_cast<T>(length);
	}

	template <MathScalar T, typename TValue>
	static T quadraticLength(const TValue& p0, const TValue& p1, const TValue& p2, uint32 subdivisions) noexcept
	{
		ASSERT(subdivisions > 0, "Curve subdivisions must be positive.");
		const detail::CurveSample<T> controls[]{detail::CurveRead<T>(p0), detail::CurveRead<T>(p1), detail::CurveRead<T>(p2)};
		return curveLength<T>(detail::ECurveKind::Quadratic, controls, subdivisions);
	}

	template <MathScalar T, typename TValue>
	static T fourPointLength(detail::ECurveKind kind, const TValue& p0, const TValue& p1, const TValue& p2, const TValue& p3, uint32 subdivisions) noexcept
	{
		ASSERT(subdivisions > 0, "Curve subdivisions must be positive.");
		const detail::CurveSample<T> controls[]{detail::CurveRead<T>(p0), detail::CurveRead<T>(p1), detail::CurveRead<T>(p2), detail::CurveRead<T>(p3)};
		return curveLength<T>(kind, controls, subdivisions);
	}

	template <MathScalar T>
	T ApproximateQuadraticBezierLength(T p0, T p1, T p2, uint32 subdivisions) noexcept
	{
		return quadraticLength<T>(p0, p1, p2, subdivisions);
	}

	template <MathScalar T>
	T ApproximateQuadraticBezierLength(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, uint32 subdivisions) noexcept
	{
		return quadraticLength<T>(p0, p1, p2, subdivisions);
	}

	template <MathScalar T>
	T ApproximateQuadraticBezierLength(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, uint32 subdivisions) noexcept
	{
		return quadraticLength<T>(p0, p1, p2, subdivisions);
	}

	template <MathScalar T>
	T ApproximateQuadraticBezierLength(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2,
	                                   uint32 subdivisions) noexcept
	{
		return quadraticLength<T>(p0, p1, p2, subdivisions);
	}

	template <MathScalar T>
	T ApproximateCubicBezierLength(T p0, T p1, T p2, T p3, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, subdivisions);
	}

	template <MathScalar T>
	T ApproximateCubicBezierLength(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, subdivisions);
	}

	template <MathScalar T>
	T ApproximateCubicBezierLength(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, subdivisions);
	}

	template <MathScalar T>
	T ApproximateCubicBezierLength(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, subdivisions);
	}

	template <MathScalar T>
	T ApproximateHermiteLength(T p0, T tangent0, T p1, T tangent1, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, subdivisions);
	}

	template <MathScalar T>
	T ApproximateHermiteLength(const Vector2<T>& p0, const Vector2<T>& tangent0, const Vector2<T>& p1, const Vector2<T>& tangent1, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, subdivisions);
	}

	template <MathScalar T>
	T ApproximateHermiteLength(const Vector3<T>& p0, const Vector3<T>& tangent0, const Vector3<T>& p1, const Vector3<T>& tangent1, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, subdivisions);
	}

	template <MathScalar T>
	T ApproximateHermiteLength(const Vector4<T>& p0, const Vector4<T>& tangent0, const Vector4<T>& p1, const Vector4<T>& tangent1, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, subdivisions);
	}

	template <MathScalar T>
	T ApproximateCatmullRomLength(T p0, T p1, T p2, T p3, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, subdivisions);
	}

	template <MathScalar T>
	T ApproximateCatmullRomLength(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, subdivisions);
	}

	template <MathScalar T>
	T ApproximateCatmullRomLength(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, subdivisions);
	}

	template <MathScalar T>
	T ApproximateCatmullRomLength(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, uint32 subdivisions) noexcept
	{
		return fourPointLength<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, subdivisions);
	}

	template float32 ApproximateQuadraticBezierLength(float32 p0, float32 p1, float32 p2, uint32 subdivisions) noexcept;
	template float64 ApproximateQuadraticBezierLength(float64 p0, float64 p1, float64 p2, uint32 subdivisions) noexcept;
	template float32 ApproximateQuadraticBezierLength(const Vector2<float32>& p0, const Vector2<float32>& p1, const Vector2<float32>& p2, uint32 subdivisions) noexcept;
	template float64 ApproximateQuadraticBezierLength(const Vector2<float64>& p0, const Vector2<float64>& p1, const Vector2<float64>& p2, uint32 subdivisions) noexcept;
	template float32 ApproximateQuadraticBezierLength(const Vector3<float32>& p0, const Vector3<float32>& p1, const Vector3<float32>& p2, uint32 subdivisions) noexcept;
	template float64 ApproximateQuadraticBezierLength(const Vector3<float64>& p0, const Vector3<float64>& p1, const Vector3<float64>& p2, uint32 subdivisions) noexcept;
	template float32 ApproximateQuadraticBezierLength(const Vector4<float32>& p0, const Vector4<float32>& p1, const Vector4<float32>& p2, uint32 subdivisions) noexcept;
	template float64 ApproximateQuadraticBezierLength(const Vector4<float64>& p0, const Vector4<float64>& p1, const Vector4<float64>& p2, uint32 subdivisions) noexcept;
	template float32 ApproximateCubicBezierLength(float32 p0, float32 p1, float32 p2, float32 p3, uint32 subdivisions) noexcept;
	template float64 ApproximateCubicBezierLength(float64 p0, float64 p1, float64 p2, float64 p3, uint32 subdivisions) noexcept;
	template float32 ApproximateCubicBezierLength(const Vector2<float32>& p0, const Vector2<float32>& p1, const Vector2<float32>& p2, const Vector2<float32>& p3, uint32 subdivisions) noexcept;
	template float64 ApproximateCubicBezierLength(const Vector2<float64>& p0, const Vector2<float64>& p1, const Vector2<float64>& p2, const Vector2<float64>& p3, uint32 subdivisions) noexcept;
	template float32 ApproximateCubicBezierLength(const Vector3<float32>& p0, const Vector3<float32>& p1, const Vector3<float32>& p2, const Vector3<float32>& p3, uint32 subdivisions) noexcept;
	template float64 ApproximateCubicBezierLength(const Vector3<float64>& p0, const Vector3<float64>& p1, const Vector3<float64>& p2, const Vector3<float64>& p3, uint32 subdivisions) noexcept;
	template float32 ApproximateCubicBezierLength(const Vector4<float32>& p0, const Vector4<float32>& p1, const Vector4<float32>& p2, const Vector4<float32>& p3, uint32 subdivisions) noexcept;
	template float64 ApproximateCubicBezierLength(const Vector4<float64>& p0, const Vector4<float64>& p1, const Vector4<float64>& p2, const Vector4<float64>& p3, uint32 subdivisions) noexcept;
	template float32 ApproximateHermiteLength(float32 p0, float32 tangent0, float32 p1, float32 tangent1, uint32 subdivisions) noexcept;
	template float64 ApproximateHermiteLength(float64 p0, float64 tangent0, float64 p1, float64 tangent1, uint32 subdivisions) noexcept;
	template float32 ApproximateHermiteLength(const Vector2<float32>& p0, const Vector2<float32>& tangent0, const Vector2<float32>& p1, const Vector2<float32>& tangent1, uint32 subdivisions) noexcept;
	template float64 ApproximateHermiteLength(const Vector2<float64>& p0, const Vector2<float64>& tangent0, const Vector2<float64>& p1, const Vector2<float64>& tangent1, uint32 subdivisions) noexcept;
	template float32 ApproximateHermiteLength(const Vector3<float32>& p0, const Vector3<float32>& tangent0, const Vector3<float32>& p1, const Vector3<float32>& tangent1, uint32 subdivisions) noexcept;
	template float64 ApproximateHermiteLength(const Vector3<float64>& p0, const Vector3<float64>& tangent0, const Vector3<float64>& p1, const Vector3<float64>& tangent1, uint32 subdivisions) noexcept;
	template float32 ApproximateHermiteLength(const Vector4<float32>& p0, const Vector4<float32>& tangent0, const Vector4<float32>& p1, const Vector4<float32>& tangent1, uint32 subdivisions) noexcept;
	template float64 ApproximateHermiteLength(const Vector4<float64>& p0, const Vector4<float64>& tangent0, const Vector4<float64>& p1, const Vector4<float64>& tangent1, uint32 subdivisions) noexcept;
	template float32 ApproximateCatmullRomLength(float32 p0, float32 p1, float32 p2, float32 p3, uint32 subdivisions) noexcept;
	template float64 ApproximateCatmullRomLength(float64 p0, float64 p1, float64 p2, float64 p3, uint32 subdivisions) noexcept;
	template float32 ApproximateCatmullRomLength(const Vector2<float32>& p0, const Vector2<float32>& p1, const Vector2<float32>& p2, const Vector2<float32>& p3, uint32 subdivisions) noexcept;
	template float64 ApproximateCatmullRomLength(const Vector2<float64>& p0, const Vector2<float64>& p1, const Vector2<float64>& p2, const Vector2<float64>& p3, uint32 subdivisions) noexcept;
	template float32 ApproximateCatmullRomLength(const Vector3<float32>& p0, const Vector3<float32>& p1, const Vector3<float32>& p2, const Vector3<float32>& p3, uint32 subdivisions) noexcept;
	template float64 ApproximateCatmullRomLength(const Vector3<float64>& p0, const Vector3<float64>& p1, const Vector3<float64>& p2, const Vector3<float64>& p3, uint32 subdivisions) noexcept;
	template float32 ApproximateCatmullRomLength(const Vector4<float32>& p0, const Vector4<float32>& p1, const Vector4<float32>& p2, const Vector4<float32>& p3, uint32 subdivisions) noexcept;
	template float64 ApproximateCatmullRomLength(const Vector4<float64>& p0, const Vector4<float64>& p1, const Vector4<float64>& p2, const Vector4<float64>& p3, uint32 subdivisions) noexcept;
} // namespace rox::math
