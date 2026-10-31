#include <Plane.h>
#include <Numeric.hpp>
#include <Comparison.hpp>

#include <cmath>

namespace rox::math
{
	template <MathScalar T>
	Plane<T> Plane<T>::FromPointNormal(const Vector3<T>& point, const Vector3<T>& normal) noexcept
	{
		const Vector3<T> normalizedNormal = normal.Normalized();
		return {normalizedNormal, -normalizedNormal.Dot(point)};
	}

	template <MathScalar T>
	bool Plane<T>::TryNormalize(Plane<T>* outNormalized, T epsilon) const noexcept
	{
		ASSERT(outNormalized != nullptr, "Normalized plane output must not be null.");
		ASSERT(rox::core::IsFinite(epsilon) && epsilon >= T(0), "Plane normalization epsilon must be finite and non-negative.");
		
		if (!Normal.IsFinite() || !rox::core::IsFinite(Distance))
		{
			return false;
		}

		const T scale = rox::core::Max({rox::core::Abs(Normal.X), rox::core::Abs(Normal.Y), rox::core::Abs(Normal.Z)});
		if (scale == T(0))
		{
			return false;
		}

		const Vector3<T> scaled = Normal / scale;
		const T length = scaled.Length();
		if (scale <= epsilon / length)
		{
			return false;
		}

		const T distance = (Distance / scale) / length;
		if (!rox::core::IsFinite(distance))
		{
			return false;
		}

		*outNormalized = {scaled / length, distance};
		return true;
	}

	template <MathScalar T>
	Plane<T> Plane<T>::Normalized(T epsilon) const noexcept
	{
		Plane<T> result;
		const bool bNormalized = TryNormalize(&result, epsilon);
		ASSERT(bNormalized, "Plane must have finite coefficients and a usable normal.");
		return result;
	}

	template struct Plane<float32>;
	template struct Plane<float64>;
} // namespace rox::math
