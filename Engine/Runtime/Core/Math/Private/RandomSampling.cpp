#include <RandomSampling.h>
#include <MathConstants.hpp>
#include <Assertion.h>
#include <Types.h>
#include <Numeric.hpp>
#include <Comparison.hpp>

#include <algorithm>
#include <cmath>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		T unit53(RandomStream& stream) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				return static_cast<T>(stream.NextU64() >> 40) * T(0x1p-24);
			}
			else
			{
				return stream.Real01<T>();
			}
		}
	} // namespace

	template <MathScalar T>
	core::usize RandomWeightedIndex(RandomStream& stream, const T* weights, core::usize count) noexcept
	{
		ASSERT(weights != nullptr && count > 0, "Random weights require a non-empty valid range.");
		T largest = 0;
		for (core::usize i = 0; i < count; ++i)
		{
			ASSERT(core::IsFinite(weights[i]) && weights[i] >= 0, "Random weights must be finite and nonnegative.");
			largest = core::Max(largest, weights[i]);
		}

		ASSERT(largest > 0, "Random weights require positive total weight.");
		T total = 0;
		core::usize lastPositive = 0;

		for (core::usize i = 0; i < count; ++i)
		{
			ASSERT(core::IsFinite(weights[i]) && weights[i] >= 0, "Random weights must be finite and nonnegative.");
			total += weights[i] / largest;

			if (weights[i] > 0)
			{
				lastPositive = i;
			}
		}

		ASSERT(total > 0, "Random weights require positive total weight.");
		const T target = core::Min(unit53<T>(stream) * total, core::NextAfter(total, T(0.0)));
		T cumulative = 0;

		for (core::usize i = 0; i < lastPositive; ++i)
		{
			cumulative += weights[i] / largest;

			if (target < cumulative)
			{
				return i;
			}
		}

		return lastPositive;
	}

	template <MathScalar T>
	Vector2<T> RandomUnitCircle(RandomStream& stream) noexcept
	{
		const T angle = 2 * core::PI<T> * stream.Real01<T>();
		return {static_cast<T>(core::Cos(angle)), static_cast<T>(core::Sin(angle))};
	}

	template <MathScalar T>
	Vector2<T> RandomInUnitDisk(RandomStream& stream) noexcept
	{
		const Vector2<T> direction = RandomUnitCircle<T>(stream);
		return direction * static_cast<T>(core::Sqrt(static_cast<T>(stream.Real01<T>())));
	}

	template <MathScalar T>
	Vector3<T> RandomUnitSphere(RandomStream& stream) noexcept
	{
		const T z = 1 - 2 * static_cast<T>(stream.Real01<T>());
		const T angle = 2 * core::PI<T> * stream.Real01<T>();
		const T radius = core::Sqrt(core::Max(T(0.0), 1 - z * z));
		return {static_cast<T>(radius * core::Cos(angle)), static_cast<T>(radius * core::Sin(angle)), static_cast<T>(z)};
	}

	template <MathScalar T>
	Vector3<T> RandomInUnitSphere(RandomStream& stream) noexcept
	{
		const Vector3<T> direction = RandomUnitSphere<T>(stream);
		return direction * static_cast<T>(core::Cbrt(static_cast<T>(stream.Real01<T>())));
	}

	template <MathScalar T>
	Vector3<T> RandomCone(RandomStream& stream, const Vector3<T>& unitAxis, T halfAngleRadians) noexcept
	{
		ASSERT(unitAxis.IsFinite() && core::Abs(unitAxis.Length() - 1) <= T(1.0e-4) && core::IsFinite(halfAngleRadians) && halfAngleRadians >= 0 && halfAngleRadians <= static_cast<T>(core::PI<T>),
		       "Random cone requires a finite unit axis and angle in [0, pi].");
		const Vector3<T> axis = unitAxis.Normalized();
		const T cosine = 1 - stream.Real01<T>() * (1 - core::Cos(static_cast<T>(halfAngleRadians)));
		const T angle = 2 * core::PI<T> * stream.Real01<T>();
		const T sine = core::Sqrt(core::Max(T(0.0), 1 - cosine * cosine));
		const Vector3<T> helper =
		    core::Abs(axis.X) <= core::Abs(axis.Y) && core::Abs(axis.X) <= core::Abs(axis.Z)
		        ? Vector3<T>{1, 0, 0}
		        : (core::Abs(axis.Y) <= core::Abs(axis.Z) ? Vector3<T>{0, 1, 0} : Vector3<T>{0, 0, 1});
		const Vector3<T> tangent = helper.Cross(axis).Normalized();
		const Vector3<T> bitangent = axis.Cross(tangent);
		return axis * static_cast<T>(cosine) + tangent * static_cast<T>(sine * core::Cos(angle)) + bitangent * static_cast<T>(sine * core::Sin(angle));
	}

	template <MathScalar T>
	T RandomNormal(RandomStream& stream) noexcept
	{
		const T radius = core::Sqrt(-2 * core::Log1p(-static_cast<T>(stream.Real01<T>())));
		const T angle = 2 * core::PI<T> * stream.Real01<T>();
		return static_cast<T>(radius * core::Cos(angle));
	}

	template <MathScalar T>
	T RandomExponential(RandomStream& stream) noexcept
	{
		return static_cast<T>(-core::Log1p(-static_cast<T>(stream.Real01<T>())));
	}

	template core::usize RandomWeightedIndex(RandomStream& stream, const float32* weights, core::usize count) noexcept;
	template core::usize RandomWeightedIndex(RandomStream& stream, const float64* weights, core::usize count) noexcept;
	template Vector2<float32> RandomUnitCircle(RandomStream& stream) noexcept;
	template Vector2<float64> RandomUnitCircle(RandomStream& stream) noexcept;
	template Vector2<float32> RandomInUnitDisk(RandomStream& stream) noexcept;
	template Vector2<float64> RandomInUnitDisk(RandomStream& stream) noexcept;
	template Vector3<float32> RandomUnitSphere(RandomStream& stream) noexcept;
	template Vector3<float64> RandomUnitSphere(RandomStream& stream) noexcept;
	template Vector3<float32> RandomInUnitSphere(RandomStream& stream) noexcept;
	template Vector3<float64> RandomInUnitSphere(RandomStream& stream) noexcept;
	template Vector3<float32> RandomCone(RandomStream& stream, const Vector3<float32>& unitAxis, float32 halfAngleRadians) noexcept;
	template Vector3<float64> RandomCone(RandomStream& stream, const Vector3<float64>& unitAxis, float64 halfAngleRadians) noexcept;
	template float32 RandomNormal(RandomStream& stream) noexcept;
	template float64 RandomNormal(RandomStream& stream) noexcept;
	template float32 RandomExponential(RandomStream& stream) noexcept;
	template float64 RandomExponential(RandomStream& stream) noexcept;
} // namespace rox::math
