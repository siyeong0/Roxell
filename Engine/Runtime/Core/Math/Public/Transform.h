#pragma once

#include <MathScalar.hpp>

#include <AffineTransform.h>
#include <Matrix4x4.hpp>
#include <Quaternion.hpp>
#include <TypeTraits.hpp>
#include <Vector3.hpp>

#include <cstddef>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T = float32>
	struct Transform final
	{
		Vector3<T> Translation = Vector3<T>::Zero();
		Quaternion<T> Rotation = Quaternion<T>::Identity();
		Vector3<T> Scale = Vector3<T>::One();

		constexpr Transform() noexcept = default;

		constexpr Transform(const Vector3<T>& translation, const Quaternion<T>& rotation, const Vector3<T>& scale) noexcept
		    : Translation(translation)
			, Rotation(rotation)
			, Scale(scale)
		{
		}

		[[nodiscard]] static constexpr Transform<T> Identity() noexcept
		{
			return {};
		}

		[[nodiscard]] Matrix4x4<T> ToMatrix() const noexcept;
		[[nodiscard]] AffineTransform<T> ToAffine() const noexcept;
		[[nodiscard]] Vector3<T> TransformPosition(const Vector3<T>& position) const noexcept;
		[[nodiscard]] Vector3<T> TransformDirection(const Vector3<T>& direction) const noexcept;
		[[nodiscard]] Vector3<T> TransformNormal(const Vector3<T>& normal) const noexcept;
		[[nodiscard]] Vector3<T> InverseTransformPosition(const Vector3<T>& position) const noexcept;
		[[nodiscard]] Vector3<T> InverseTransformDirection(const Vector3<T>& direction) const noexcept;
		[[nodiscard]] AffineTransform<T> ComposeAffine(const Transform<T>& parent) const noexcept;
		[[nodiscard]] AffineTransform<T> RelativeAffine(const Transform<T>& parentWorld) const noexcept;
		[[nodiscard]] static bool DecomposeTRS(const Matrix4x4<T>& matrix, Transform<T>* outTransform) noexcept;

		template <MathScalar U>
		explicit constexpr Transform(const Transform<U>& other) noexcept
		    : Translation(other.Translation)
			, Rotation(other.Rotation)
			, Scale(other.Scale)
		{
		}
	};

	static_assert(sizeof(Transform<float32>) == sizeof(float32) * 10);
	static_assert(sizeof(Transform<float64>) == sizeof(float64) * 10);
	static_assert(offsetof(Transform<float32>, Rotation) == sizeof(float32) * 3);
	static_assert(offsetof(Transform<float64>, Rotation) == sizeof(float64) * 3);
	static_assert(core::IsStandardLayout<Transform<float32>> && core::IsTriviallyCopyable<Transform<float32>>);
	static_assert(core::IsStandardLayout<Transform<float64>> && core::IsTriviallyCopyable<Transform<float64>>);
} // namespace rox::math
