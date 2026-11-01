#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Vector3.hpp>

#include <limits>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T = float32>
	struct Box final
	{
		Vector3<T> Min{core::NumericLimits<T>::max(), core::NumericLimits<T>::max(), core::NumericLimits<T>::max()};
		Vector3<T> Max{core::NumericLimits<T>::lowest(), core::NumericLimits<T>::lowest(), core::NumericLimits<T>::lowest()};

		constexpr Box() noexcept = default;

		constexpr Box(const Vector3<T>& min, const Vector3<T>& max) noexcept : Min(min), Max(max)
		{
		}

		[[nodiscard]] static constexpr Box<T> Empty() noexcept
		{
			return {};
		}

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] Vector3<T> Center() const noexcept;
		[[nodiscard]] Vector3<T> Size() const noexcept;
		[[nodiscard]] Vector3<T> Extents() const noexcept;
		[[nodiscard]] T Volume() const noexcept;

		void Encapsulate(const Vector3<T>& point) noexcept;
		void Encapsulate(const Box<T>& other) noexcept;
		[[nodiscard]] bool Contains(const Vector3<T>& point) const noexcept;
		[[nodiscard]] bool Overlaps(const Box<T>& other) const noexcept;

		template <MathScalar U>
		explicit constexpr Box(const Box<U>& other) noexcept : Min(other.Min), Max(other.Max)
		{
		}
	};

	static_assert(sizeof(Box<float32>) == sizeof(float32) * 6);
	static_assert(sizeof(Box<float64>) == sizeof(float64) * 6);
	static_assert(core::IsStandardLayout<Box<float32>> && core::IsTriviallyCopyable<Box<float32>>);
	static_assert(core::IsStandardLayout<Box<float64>> && core::IsTriviallyCopyable<Box<float64>>);
} // namespace rox::math
