#pragma once

#include <MathScalar.hpp>

#include <Box.h>
#include <TypeTraits.hpp>

namespace rox::math
{
	template <MathScalar T = float32>
	struct Triangle final
	{
		Vector3<T> A = Vector3<T>::Zero();
		Vector3<T> B = Vector3<T>::Zero();
		Vector3<T> C = Vector3<T>::Zero();
		constexpr Triangle() noexcept = default;

		constexpr Triangle(const Vector3<T>& a, const Vector3<T>& b, const Vector3<T>& c) noexcept 
			: A(a)
			, B(b)
			, C(c)
		{
		}

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool IsDegenerate() const noexcept;
		[[nodiscard]] T Area() const noexcept;
		[[nodiscard]] Vector3<T> Centroid() const noexcept;
		[[nodiscard]] Vector3<T> Normal() const noexcept;
		[[nodiscard]] Box<T> GetBounds() const noexcept;
		[[nodiscard]] bool Barycentric(const Vector3<T>& point, Vector3<T>* outWeights) const noexcept;
		[[nodiscard]] Vector3<T> ClosestPoint(const Vector3<T>& point) const noexcept;
		[[nodiscard]] T DistanceSquared(const Vector3<T>& point) const noexcept;

		template <MathScalar U>
		explicit constexpr Triangle(const Triangle<U>& other) noexcept 
		: A(other.A)
		, B(other.B)
		, C(other.C)
		{
		}
	};

	static_assert(sizeof(Triangle<float32>) == 3 * sizeof(Vector3<float32>));
	static_assert(sizeof(Triangle<float64>) == 3 * sizeof(Vector3<float64>));
	static_assert(rox::core::IsStandardLayout<Triangle<float32>> && rox::core::IsTriviallyCopyable<Triangle<float32>>);
	static_assert(rox::core::IsStandardLayout<Triangle<float64>> && rox::core::IsTriviallyCopyable<Triangle<float64>>);
} // namespace rox::math
