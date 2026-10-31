#pragma once

#include <MathScalar.hpp>

#include <Assertion.h>
#include <Numeric.hpp>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Vector3.hpp>

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace rox::math
{
	template <MathScalar T>
	struct Matrix3x3;
	template <MathScalar T>
	struct Matrix4x4;
	template <MathScalar T>
	struct QuaternionSwingTwist;

	template <MathScalar T = float32>
	struct QuaternionAxisAngle final
	{
		Vector3<T> Axis = Vector3<T>::UnitX();
		T Radians = T(0.0);
	};

	template <MathScalar T = float32>
	struct Quaternion final
	{
		T X = T(0.0);
		T Y = T(0.0);
		T Z = T(0.0);
		T W = T(1.0);

		constexpr Quaternion() noexcept = default;

		constexpr Quaternion(T x, T y, T z, T w) noexcept : X(x), Y(y), Z(z), W(w)
		{
		}

		[[nodiscard]] static constexpr Quaternion<T> Identity() noexcept
		{
			return {};
		}

		[[nodiscard]] constexpr T Dot(const Quaternion<T>& other) const noexcept
		{
			return X * other.X + Y * other.Y + Z * other.Z + W * other.W;
		}

		[[nodiscard]] constexpr T LengthSquared() const noexcept
		{
			return Dot(*this);
		}

		[[nodiscard]] T Length() const noexcept;

		[[nodiscard]] bool IsFinite() const noexcept
		{
			return rox::core::IsFinite(X) && rox::core::IsFinite(Y) && rox::core::IsFinite(Z) && rox::core::IsFinite(W);
		}

		[[nodiscard]] constexpr Quaternion<T> Conjugated() const noexcept
		{
			return {-X, -Y, -Z, W};
		}

		[[nodiscard]] bool TryNormalize(Quaternion<T>* outNormalized, T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] Quaternion<T> Normalized(T epsilon = T(1.0e-8)) const noexcept;

		void Normalize(T epsilon = T(1.0e-8)) noexcept
		{
			*this = Normalized(epsilon);
		}

		[[nodiscard]] bool TryInverse(Quaternion<T>* outInverse, T epsilon = T(1.0e-8)) const noexcept;
		[[nodiscard]] Quaternion<T> Inversed(T epsilon = T(1.0e-8)) const noexcept;

		[[nodiscard]] static Quaternion<T> FromAxisAngle(const Vector3<T>& axis, T radians) noexcept;
		[[nodiscard]] static Quaternion<T> FromEulerXYZ(const Vector3<T>& radians) noexcept;
		[[nodiscard]] static Quaternion<T> Nlerp(const Quaternion<T>& from, const Quaternion<T>& to, T alpha) noexcept;
		[[nodiscard]] static Quaternion<T> Slerp(const Quaternion<T>& from, const Quaternion<T>& to, T alpha) noexcept;
		[[nodiscard]] static Quaternion<T> FromToRotation(const Vector3<T>& from, const Vector3<T>& to) noexcept;
		[[nodiscard]] static Quaternion<T> FromRotationMatrix(const Matrix3x3<T>& matrix) noexcept;
		[[nodiscard]] static Quaternion<T> LookRotation(const Vector3<T>& forward, const Vector3<T>& up = Vector3<T>::UnitY()) noexcept;
		[[nodiscard]] T AngularDistance(const Quaternion<T>& other) const noexcept;
		[[nodiscard]] static Quaternion<T> RotateTowards(const Quaternion<T>& from, const Quaternion<T>& to, T maxRadiansDelta) noexcept;
		[[nodiscard]] QuaternionAxisAngle<T> ToAxisAngle() const noexcept;
		[[nodiscard]] Vector3<T> ToEulerXYZ() const noexcept;
		[[nodiscard]] QuaternionSwingTwist<T> DecomposeSwingTwist(const Vector3<T>& twistAxis) const noexcept;

		// Principal full-angle rotation vector in radians; Exp accepts any finite rotation vector.
		[[nodiscard]] Vector3<T> RotationLog() const noexcept;
		[[nodiscard]] static Quaternion<T> RotationExp(const Vector3<T>& rotationVector) noexcept;

		// Controls for uniformly spaced keys; duplicate the endpoint key at open sequence boundaries.
		[[nodiscard]] static Quaternion<T> SquadTangent(const Quaternion<T>& previous, const Quaternion<T>& current, const Quaternion<T>& next) noexcept;
		[[nodiscard]] static Quaternion<T> Squad(const Quaternion<T>& from, const Quaternion<T>& to, const Quaternion<T>& fromTangent, const Quaternion<T>& toTangent, T alpha) noexcept;

		[[nodiscard]] Vector3<T> RotateVector(const Vector3<T>& vector) const noexcept;
		[[nodiscard]] Matrix3x3<T> ToMatrix3x3() const noexcept;
		[[nodiscard]] Matrix4x4<T> ToMatrix4x4() const noexcept;

		[[nodiscard]] constexpr Quaternion<T> operator-() const noexcept
		{
			return {-X, -Y, -Z, -W};
		}

		[[nodiscard]] Quaternion<T> operator*(const Quaternion<T>& other) const noexcept;

		[[nodiscard]] constexpr Quaternion<T> operator*(T scalar) const noexcept
		{
			return {X * scalar, Y * scalar, Z * scalar, W * scalar};
		}

		[[nodiscard]] constexpr Quaternion<T> operator+(const Quaternion<T>& other) const noexcept
		{
			return {X + other.X, Y + other.Y, Z + other.Z, W + other.W};
		}

		[[nodiscard]] constexpr bool operator==(const Quaternion<T>&) const noexcept = default;

		template <MathScalar U>
		explicit constexpr Quaternion(const Quaternion<U>& other) noexcept
		    : X(static_cast<T>(other.X))
			, Y(static_cast<T>(other.Y))
			, Z(static_cast<T>(other.Z))
			, W(static_cast<T>(other.W))
		{
		}
	};

	template <MathScalar T = float32>
	struct QuaternionSwingTwist final
	{
		Quaternion<T> Swing = Quaternion<T>::Identity();
		Quaternion<T> Twist = Quaternion<T>::Identity();
	};

	static_assert(rox::core::IsStandardLayout<QuaternionAxisAngle<float32>> && rox::core::IsTriviallyCopyable<QuaternionAxisAngle<float32>>);
	static_assert(rox::core::IsStandardLayout<QuaternionAxisAngle<float64>> && rox::core::IsTriviallyCopyable<QuaternionAxisAngle<float64>>);
	static_assert(rox::core::IsStandardLayout<QuaternionSwingTwist<float32>> && rox::core::IsTriviallyCopyable<QuaternionSwingTwist<float32>>);
	static_assert(rox::core::IsStandardLayout<QuaternionSwingTwist<float64>> && rox::core::IsTriviallyCopyable<QuaternionSwingTwist<float64>>);
	static_assert(sizeof(Quaternion<float32>) == sizeof(float32) * 4);
	static_assert(sizeof(Quaternion<float64>) == sizeof(float64) * 4);
	static_assert(offsetof(Quaternion<float32>, W) == sizeof(float32) * 3);
	static_assert(offsetof(Quaternion<float64>, W) == sizeof(float64) * 3);
	static_assert(rox::core::IsStandardLayout<Quaternion<float32>> && rox::core::IsTriviallyCopyable<Quaternion<float32>>);
	static_assert(rox::core::IsStandardLayout<Quaternion<float64>> && rox::core::IsTriviallyCopyable<Quaternion<float64>>);
} // namespace rox::math
