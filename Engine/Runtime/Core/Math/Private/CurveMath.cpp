#include <CurveMath.h>
#include "CurveDetail.hpp"

namespace rox::math
{
	template <MathScalar T, typename TValue>
	static TValue quadraticCurve(const TValue& p0, const TValue& p1, const TValue& p2, T t, bool derivative) noexcept
	{
		detail::CurveValidateParameter<T>(t);
		const detail::CurveSample<T> controls[]{detail::CurveRead<T>(p0), detail::CurveRead<T>(p1), detail::CurveRead<T>(p2)};
		return detail::CurveNarrow<T, TValue>(detail::CurveEvaluate<T>(detail::ECurveKind::Quadratic, controls, t, derivative));
	}

	template <MathScalar T>
	T QuadraticBezier(T p0, T p1, T p2, T t) noexcept
	{
		return quadraticCurve<T>(p0, p1, p2, t, false);
	}

	template <MathScalar T>
	Vector2<T> QuadraticBezier(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, T t) noexcept
	{
		return quadraticCurve<T>(p0, p1, p2, t, false);
	}

	template <MathScalar T>
	Vector3<T> QuadraticBezier(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, T t) noexcept
	{
		return quadraticCurve<T>(p0, p1, p2, t, false);
	}

	template <MathScalar T>
	Vector4<T> QuadraticBezier(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, T t) noexcept
	{
		return quadraticCurve<T>(p0, p1, p2, t, false);
	}

	template <MathScalar T>
	T QuadraticBezierTangent(T p0, T p1, T p2, T t) noexcept
	{
		return quadraticCurve<T>(p0, p1, p2, t, true);
	}

	template <MathScalar T>
	Vector2<T> QuadraticBezierTangent(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, T t) noexcept
	{
		return quadraticCurve<T>(p0, p1, p2, t, true);
	}

	template <MathScalar T>
	Vector3<T> QuadraticBezierTangent(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, T t) noexcept
	{
		return quadraticCurve<T>(p0, p1, p2, t, true);
	}

	template <MathScalar T>
	Vector4<T> QuadraticBezierTangent(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, T t) noexcept
	{
		return quadraticCurve<T>(p0, p1, p2, t, true);
	}

	template <MathScalar T, typename TValue>
	static TValue fourPointCurve(detail::ECurveKind kind, const TValue& p0, const TValue& p1, const TValue& p2, const TValue& p3, T t, bool derivative) noexcept
	{
		detail::CurveValidateParameter<T>(t);
		const detail::CurveSample<T> controls[]{detail::CurveRead<T>(p0), detail::CurveRead<T>(p1), detail::CurveRead<T>(p2), detail::CurveRead<T>(p3)};
		return detail::CurveNarrow<T, TValue>(detail::CurveEvaluate<T>(kind, controls, t, derivative));
	}

	template <MathScalar T>
	T CubicBezier(T p0, T p1, T p2, T p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, t, false);
	}

	template <MathScalar T>
	Vector2<T> CubicBezier(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, t, false);
	}

	template <MathScalar T>
	Vector3<T> CubicBezier(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, t, false);
	}

	template <MathScalar T>
	Vector4<T> CubicBezier(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, t, false);
	}

	template <MathScalar T>
	T CubicBezierTangent(T p0, T p1, T p2, T p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, t, true);
	}

	template <MathScalar T>
	Vector2<T> CubicBezierTangent(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, t, true);
	}

	template <MathScalar T>
	Vector3<T> CubicBezierTangent(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, t, true);
	}

	template <MathScalar T>
	Vector4<T> CubicBezierTangent(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Cubic, p0, p1, p2, p3, t, true);
	}

	template <MathScalar T>
	T Hermite(T p0, T tangent0, T p1, T tangent1, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, t, false);
	}

	template <MathScalar T>
	Vector2<T> Hermite(const Vector2<T>& p0, const Vector2<T>& tangent0, const Vector2<T>& p1, const Vector2<T>& tangent1, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, t, false);
	}

	template <MathScalar T>
	Vector3<T> Hermite(const Vector3<T>& p0, const Vector3<T>& tangent0, const Vector3<T>& p1, const Vector3<T>& tangent1, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, t, false);
	}

	template <MathScalar T>
	Vector4<T> Hermite(const Vector4<T>& p0, const Vector4<T>& tangent0, const Vector4<T>& p1, const Vector4<T>& tangent1, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, t, false);
	}

	template <MathScalar T>
	T HermiteTangent(T p0, T tangent0, T p1, T tangent1, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, t, true);
	}

	template <MathScalar T>
	Vector2<T> HermiteTangent(const Vector2<T>& p0, const Vector2<T>& tangent0, const Vector2<T>& p1, const Vector2<T>& tangent1, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, t, true);
	}

	template <MathScalar T>
	Vector3<T> HermiteTangent(const Vector3<T>& p0, const Vector3<T>& tangent0, const Vector3<T>& p1, const Vector3<T>& tangent1, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, t, true);
	}

	template <MathScalar T>
	Vector4<T> HermiteTangent(const Vector4<T>& p0, const Vector4<T>& tangent0, const Vector4<T>& p1, const Vector4<T>& tangent1, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::Hermite, p0, tangent0, p1, tangent1, t, true);
	}

	template <MathScalar T>
	T CatmullRom(T p0, T p1, T p2, T p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, t, false);
	}

	template <MathScalar T>
	Vector2<T> CatmullRom(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, t, false);
	}

	template <MathScalar T>
	Vector3<T> CatmullRom(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, t, false);
	}

	template <MathScalar T>
	Vector4<T> CatmullRom(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, t, false);
	}

	template <MathScalar T>
	T CatmullRomTangent(T p0, T p1, T p2, T p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, t, true);
	}

	template <MathScalar T>
	Vector2<T> CatmullRomTangent(const Vector2<T>& p0, const Vector2<T>& p1, const Vector2<T>& p2, const Vector2<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, t, true);
	}

	template <MathScalar T>
	Vector3<T> CatmullRomTangent(const Vector3<T>& p0, const Vector3<T>& p1, const Vector3<T>& p2, const Vector3<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, t, true);
	}

	template <MathScalar T>
	Vector4<T> CatmullRomTangent(const Vector4<T>& p0, const Vector4<T>& p1, const Vector4<T>& p2, const Vector4<T>& p3, T t) noexcept
	{
		return fourPointCurve<T>(detail::ECurveKind::CatmullRom, p0, p1, p2, p3, t, true);
	}

	template float32 QuadraticBezier(float32 p0, float32 p1, float32 p2, float32 t) noexcept;
	template float64 QuadraticBezier(float64 p0, float64 p1, float64 p2, float64 t) noexcept;
	template Vector2<float32> QuadraticBezier(const Vector2<float32>& p0, const Vector2<float32>& p1, const Vector2<float32>& p2, float32 t) noexcept;
	template Vector2<float64> QuadraticBezier(const Vector2<float64>& p0, const Vector2<float64>& p1, const Vector2<float64>& p2, float64 t) noexcept;
	template Vector3<float32> QuadraticBezier(const Vector3<float32>& p0, const Vector3<float32>& p1, const Vector3<float32>& p2, float32 t) noexcept;
	template Vector3<float64> QuadraticBezier(const Vector3<float64>& p0, const Vector3<float64>& p1, const Vector3<float64>& p2, float64 t) noexcept;
	template Vector4<float32> QuadraticBezier(const Vector4<float32>& p0, const Vector4<float32>& p1, const Vector4<float32>& p2, float32 t) noexcept;
	template Vector4<float64> QuadraticBezier(const Vector4<float64>& p0, const Vector4<float64>& p1, const Vector4<float64>& p2, float64 t) noexcept;
	template float32 QuadraticBezierTangent(float32 p0, float32 p1, float32 p2, float32 t) noexcept;
	template float64 QuadraticBezierTangent(float64 p0, float64 p1, float64 p2, float64 t) noexcept;
	template Vector2<float32> QuadraticBezierTangent(const Vector2<float32>& p0, const Vector2<float32>& p1, const Vector2<float32>& p2, float32 t) noexcept;
	template Vector2<float64> QuadraticBezierTangent(const Vector2<float64>& p0, const Vector2<float64>& p1, const Vector2<float64>& p2, float64 t) noexcept;
	template Vector3<float32> QuadraticBezierTangent(const Vector3<float32>& p0, const Vector3<float32>& p1, const Vector3<float32>& p2, float32 t) noexcept;
	template Vector3<float64> QuadraticBezierTangent(const Vector3<float64>& p0, const Vector3<float64>& p1, const Vector3<float64>& p2, float64 t) noexcept;
	template Vector4<float32> QuadraticBezierTangent(const Vector4<float32>& p0, const Vector4<float32>& p1, const Vector4<float32>& p2, float32 t) noexcept;
	template Vector4<float64> QuadraticBezierTangent(const Vector4<float64>& p0, const Vector4<float64>& p1, const Vector4<float64>& p2, float64 t) noexcept;
	template float32 CubicBezier(float32 p0, float32 p1, float32 p2, float32 p3, float32 t) noexcept;
	template float64 CubicBezier(float64 p0, float64 p1, float64 p2, float64 p3, float64 t) noexcept;
	template Vector2<float32> CubicBezier(const Vector2<float32>& p0, const Vector2<float32>& p1, const Vector2<float32>& p2, const Vector2<float32>& p3, float32 t) noexcept;
	template Vector2<float64> CubicBezier(const Vector2<float64>& p0, const Vector2<float64>& p1, const Vector2<float64>& p2, const Vector2<float64>& p3, float64 t) noexcept;
	template Vector3<float32> CubicBezier(const Vector3<float32>& p0, const Vector3<float32>& p1, const Vector3<float32>& p2, const Vector3<float32>& p3, float32 t) noexcept;
	template Vector3<float64> CubicBezier(const Vector3<float64>& p0, const Vector3<float64>& p1, const Vector3<float64>& p2, const Vector3<float64>& p3, float64 t) noexcept;
	template Vector4<float32> CubicBezier(const Vector4<float32>& p0, const Vector4<float32>& p1, const Vector4<float32>& p2, const Vector4<float32>& p3, float32 t) noexcept;
	template Vector4<float64> CubicBezier(const Vector4<float64>& p0, const Vector4<float64>& p1, const Vector4<float64>& p2, const Vector4<float64>& p3, float64 t) noexcept;
	template float32 CubicBezierTangent(float32 p0, float32 p1, float32 p2, float32 p3, float32 t) noexcept;
	template float64 CubicBezierTangent(float64 p0, float64 p1, float64 p2, float64 p3, float64 t) noexcept;
	template Vector2<float32> CubicBezierTangent(const Vector2<float32>& p0, const Vector2<float32>& p1, const Vector2<float32>& p2, const Vector2<float32>& p3, float32 t) noexcept;
	template Vector2<float64> CubicBezierTangent(const Vector2<float64>& p0, const Vector2<float64>& p1, const Vector2<float64>& p2, const Vector2<float64>& p3, float64 t) noexcept;
	template Vector3<float32> CubicBezierTangent(const Vector3<float32>& p0, const Vector3<float32>& p1, const Vector3<float32>& p2, const Vector3<float32>& p3, float32 t) noexcept;
	template Vector3<float64> CubicBezierTangent(const Vector3<float64>& p0, const Vector3<float64>& p1, const Vector3<float64>& p2, const Vector3<float64>& p3, float64 t) noexcept;
	template Vector4<float32> CubicBezierTangent(const Vector4<float32>& p0, const Vector4<float32>& p1, const Vector4<float32>& p2, const Vector4<float32>& p3, float32 t) noexcept;
	template Vector4<float64> CubicBezierTangent(const Vector4<float64>& p0, const Vector4<float64>& p1, const Vector4<float64>& p2, const Vector4<float64>& p3, float64 t) noexcept;
	template float32 Hermite(float32 p0, float32 tangent0, float32 p1, float32 tangent1, float32 t) noexcept;
	template float64 Hermite(float64 p0, float64 tangent0, float64 p1, float64 tangent1, float64 t) noexcept;
	template Vector2<float32> Hermite(const Vector2<float32>& p0, const Vector2<float32>& tangent0, const Vector2<float32>& p1, const Vector2<float32>& tangent1, float32 t) noexcept;
	template Vector2<float64> Hermite(const Vector2<float64>& p0, const Vector2<float64>& tangent0, const Vector2<float64>& p1, const Vector2<float64>& tangent1, float64 t) noexcept;
	template Vector3<float32> Hermite(const Vector3<float32>& p0, const Vector3<float32>& tangent0, const Vector3<float32>& p1, const Vector3<float32>& tangent1, float32 t) noexcept;
	template Vector3<float64> Hermite(const Vector3<float64>& p0, const Vector3<float64>& tangent0, const Vector3<float64>& p1, const Vector3<float64>& tangent1, float64 t) noexcept;
	template Vector4<float32> Hermite(const Vector4<float32>& p0, const Vector4<float32>& tangent0, const Vector4<float32>& p1, const Vector4<float32>& tangent1, float32 t) noexcept;
	template Vector4<float64> Hermite(const Vector4<float64>& p0, const Vector4<float64>& tangent0, const Vector4<float64>& p1, const Vector4<float64>& tangent1, float64 t) noexcept;
	template float32 HermiteTangent(float32 p0, float32 tangent0, float32 p1, float32 tangent1, float32 t) noexcept;
	template float64 HermiteTangent(float64 p0, float64 tangent0, float64 p1, float64 tangent1, float64 t) noexcept;
	template Vector2<float32> HermiteTangent(const Vector2<float32>& p0, const Vector2<float32>& tangent0, const Vector2<float32>& p1, const Vector2<float32>& tangent1, float32 t) noexcept;
	template Vector2<float64> HermiteTangent(const Vector2<float64>& p0, const Vector2<float64>& tangent0, const Vector2<float64>& p1, const Vector2<float64>& tangent1, float64 t) noexcept;
	template Vector3<float32> HermiteTangent(const Vector3<float32>& p0, const Vector3<float32>& tangent0, const Vector3<float32>& p1, const Vector3<float32>& tangent1, float32 t) noexcept;
	template Vector3<float64> HermiteTangent(const Vector3<float64>& p0, const Vector3<float64>& tangent0, const Vector3<float64>& p1, const Vector3<float64>& tangent1, float64 t) noexcept;
	template Vector4<float32> HermiteTangent(const Vector4<float32>& p0, const Vector4<float32>& tangent0, const Vector4<float32>& p1, const Vector4<float32>& tangent1, float32 t) noexcept;
	template Vector4<float64> HermiteTangent(const Vector4<float64>& p0, const Vector4<float64>& tangent0, const Vector4<float64>& p1, const Vector4<float64>& tangent1, float64 t) noexcept;
	template float32 CatmullRom(float32 p0, float32 p1, float32 p2, float32 p3, float32 t) noexcept;
	template float64 CatmullRom(float64 p0, float64 p1, float64 p2, float64 p3, float64 t) noexcept;
	template Vector2<float32> CatmullRom(const Vector2<float32>& p0, const Vector2<float32>& p1, const Vector2<float32>& p2, const Vector2<float32>& p3, float32 t) noexcept;
	template Vector2<float64> CatmullRom(const Vector2<float64>& p0, const Vector2<float64>& p1, const Vector2<float64>& p2, const Vector2<float64>& p3, float64 t) noexcept;
	template Vector3<float32> CatmullRom(const Vector3<float32>& p0, const Vector3<float32>& p1, const Vector3<float32>& p2, const Vector3<float32>& p3, float32 t) noexcept;
	template Vector3<float64> CatmullRom(const Vector3<float64>& p0, const Vector3<float64>& p1, const Vector3<float64>& p2, const Vector3<float64>& p3, float64 t) noexcept;
	template Vector4<float32> CatmullRom(const Vector4<float32>& p0, const Vector4<float32>& p1, const Vector4<float32>& p2, const Vector4<float32>& p3, float32 t) noexcept;
	template Vector4<float64> CatmullRom(const Vector4<float64>& p0, const Vector4<float64>& p1, const Vector4<float64>& p2, const Vector4<float64>& p3, float64 t) noexcept;
	template float32 CatmullRomTangent(float32 p0, float32 p1, float32 p2, float32 p3, float32 t) noexcept;
	template float64 CatmullRomTangent(float64 p0, float64 p1, float64 p2, float64 p3, float64 t) noexcept;
	template Vector2<float32> CatmullRomTangent(const Vector2<float32>& p0, const Vector2<float32>& p1, const Vector2<float32>& p2, const Vector2<float32>& p3, float32 t) noexcept;
	template Vector2<float64> CatmullRomTangent(const Vector2<float64>& p0, const Vector2<float64>& p1, const Vector2<float64>& p2, const Vector2<float64>& p3, float64 t) noexcept;
	template Vector3<float32> CatmullRomTangent(const Vector3<float32>& p0, const Vector3<float32>& p1, const Vector3<float32>& p2, const Vector3<float32>& p3, float32 t) noexcept;
	template Vector3<float64> CatmullRomTangent(const Vector3<float64>& p0, const Vector3<float64>& p1, const Vector3<float64>& p2, const Vector3<float64>& p3, float64 t) noexcept;
	template Vector4<float32> CatmullRomTangent(const Vector4<float32>& p0, const Vector4<float32>& p1, const Vector4<float32>& p2, const Vector4<float32>& p3, float32 t) noexcept;
	template Vector4<float64> CatmullRomTangent(const Vector4<float64>& p0, const Vector4<float64>& p1, const Vector4<float64>& p2, const Vector4<float64>& p3, float64 t) noexcept;
} // namespace rox::math
