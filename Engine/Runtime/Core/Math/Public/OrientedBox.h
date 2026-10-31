#pragma once

#include <MathScalar.hpp>

#include <Box.h>
#include <Quaternion.hpp>
#include <TypeTraits.hpp>

#include <type_traits>

namespace rox::math
{
	template <MathScalar T = float32>
	struct OrientedBox final
	{
		Vector3<T> Center = Vector3<T>::Zero();
		Vector3<T> HalfExtents = Vector3<T>::Zero();
		Quaternion<T> Rotation = Quaternion<T>::Identity();

		constexpr OrientedBox() noexcept = default;

		constexpr OrientedBox(const Vector3<T>& center, const Vector3<T>& halfExtents, const Quaternion<T>& rotation = Quaternion<T>::Identity()) noexcept
		    : Center(center), HalfExtents(halfExtents), Rotation(rotation)
		{
		}

		[[nodiscard]] bool IsValid() const noexcept;

		// Bits zero, one and two select the positive local X, Y and Z extent.
		[[nodiscard]] Vector3<T> GetCorner(uint32 index) const noexcept;
		[[nodiscard]] Box<T> GetBounds() const noexcept;
		[[nodiscard]] bool Contains(const Vector3<T>& point) const noexcept;
		[[nodiscard]] Vector3<T> ClosestPoint(const Vector3<T>& point) const noexcept;
		[[nodiscard]] T DistanceSquared(const Vector3<T>& point) const noexcept;

		template <MathScalar U>
		explicit constexpr OrientedBox(const OrientedBox<U>& other) noexcept
		    : Center(other.Center), HalfExtents(other.HalfExtents), Rotation(other.Rotation)
		{
		}
	};

	static_assert(rox::core::IsStandardLayout<OrientedBox<float32>> && rox::core::IsTriviallyCopyable<OrientedBox<float32>>);
	static_assert(rox::core::IsStandardLayout<OrientedBox<float64>> && rox::core::IsTriviallyCopyable<OrientedBox<float64>>);
} // namespace rox::math
