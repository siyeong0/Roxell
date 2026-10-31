#include <Quaternion.hpp>
#include <MathConstants.hpp>
#include <Types.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Vocabulary.hpp>
#include <Matrix3x3.hpp>
#include <Matrix4x4.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace rox::math
{
	namespace
	{
		template <MathScalar T>
		using QuaternionVector = rox::core::StaticArray<T, 3>;
		template <MathScalar T>
		using QuaternionCoefficients = rox::core::StaticArray<T, 4>;
		template <MathScalar T>
		using RotationMatrixRows = rox::core::StaticArray<QuaternionVector<T>, 3>;

		template <MathScalar T>
		bool validDirection(const Vector3<T>& v) noexcept
		{
			return v.IsFinite() && (v.X != T(0.0) || v.Y != T(0.0) || v.Z != T(0.0));
		}

		template <MathScalar T>
		bool validRotation(const Quaternion<T>& q) noexcept
		{
			return q.IsFinite() && (q.X != T(0.0) || q.Y != T(0.0) || q.Z != T(0.0) || q.W != T(0.0));
		}

		template <MathScalar T>
		T vectorLength(const QuaternionVector<T>& v) noexcept
		{
			return rox::core::Hypot(v[0], v[1], v[2]);
		}

		template <MathScalar T>
		T dot(const QuaternionVector<T>& a, const QuaternionVector<T>& b) noexcept
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		template <MathScalar T>
		QuaternionVector<T> cross(const QuaternionVector<T>& a, const QuaternionVector<T>& b) noexcept
		{
			return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
		}

		template <MathScalar T>
		QuaternionVector<T> normalizeDirection(QuaternionVector<T> v) noexcept
		{
			const T scale = rox::core::Max({rox::core::Abs(v[0]), rox::core::Abs(v[1]), rox::core::Abs(v[2])});
			for (T& lane : v)
			{
				lane /= scale;
			}

			const T length = vectorLength<T>(v);

			for (T& lane : v)
			{
				lane /= length;
			}

			return v;
		}

		template <MathScalar T>
		QuaternionCoefficients<T> canonicalRotation(QuaternionCoefficients<T> q) noexcept
		{
			const T scale = rox::core::Max({rox::core::Abs(q[0]), rox::core::Abs(q[1]), rox::core::Abs(q[2]), rox::core::Abs(q[3])});
			for (T& lane : q)
			{
				lane /= scale;
			}

			const T length = rox::core::Hypot(rox::core::Hypot(q[0], q[1]), rox::core::Hypot(q[2], q[3]));

			for (T& lane : q)
			{
				lane /= length;
			}

			bool flip = q[3] < T(0.0);

			if (q[3] == T(0.0))
			{
				for (rox::core::usize index = 0; index < 3; ++index)
				{
					if (q[index] != T(0.0))
					{
						flip = q[index] < T(0.0);
						break;
					}
				}
			}

			for (T& lane : q)
			{
				lane = lane == T(0.0) ? T(0.0) : (flip ? -lane : lane);
			}

			return q;
		}

		template <MathScalar T>
		QuaternionCoefficients<T> normalizeRotation(const Quaternion<T>& q) noexcept
		{
			return canonicalRotation<T>({q.X, q.Y, q.Z, q.W});
		}

		template <MathScalar T>
		Quaternion<T> makeRotation(QuaternionCoefficients<T> q) noexcept
		{
			q = canonicalRotation<T>(q);
			Quaternion<T> result{static_cast<T>(q[0]), static_cast<T>(q[1]), static_cast<T>(q[2]), static_cast<T>(q[3])};
			// Rounding can turn a tiny real part into zero and expose the half-turn sign tie.
			if (result.W == T(0.0) && (result.X < T(0.0) || (result.X == T(0.0) && (result.Y < T(0.0) || (result.Y == T(0.0) && result.Z < T(0.0))))))
			{
				result = -result;
			}

			if (result.X == T(0.0))
			{
				result.X = T(0.0);
			}

			if (result.Y == T(0.0))
			{
				result.Y = T(0.0);
			}

			if (result.Z == T(0.0))
			{
				result.Z = T(0.0);
			}

			if (result.W == T(0.0))
			{
				result.W = T(0.0);
			}

			return result;
		}

		template <MathScalar T>
		QuaternionCoefficients<T> multiplyRotation(const QuaternionCoefficients<T>& a, const QuaternionCoefficients<T>& b) noexcept
		{
			return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
			        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
			        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
			        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
		}

		template <MathScalar T>
		QuaternionCoefficients<T> conjugateRotation(QuaternionCoefficients<T> q) noexcept
		{
			q[0] = -q[0];
			q[1] = -q[1];
			q[2] = -q[2];
			return q;
		}

		template <MathScalar T>
		QuaternionVector<T> perpendicularDirection(const QuaternionVector<T>& v) noexcept
		{
			const T x = rox::core::Abs(v[0]);
			const T y = rox::core::Abs(v[1]);
			const T z = rox::core::Abs(v[2]);
			QuaternionVector<T> cardinal{};
			cardinal[x <= y && x <= z ? 0 : (y <= z ? 1 : 2)] = T(1.0);
			return normalizeDirection<T>(cross<T>(v, cardinal));
		}

		template <MathScalar T>
		QuaternionVector<T> rotationLog(QuaternionCoefficients<T> q) noexcept
		{
			q = canonicalRotation<T>(q);
			const T sine = rox::core::Hypot(q[0], q[1], q[2]);

			if (sine == T(0.0))
			{
				return {};
			}

			const T scale = T(2.0) * rox::core::Atan2(sine, q[3]) / sine;
			return {q[0] * scale, q[1] * scale, q[2] * scale};
		}

		template <MathScalar T>
		QuaternionCoefficients<T> rotationExp(const QuaternionVector<T>& v) noexcept
		{
			const T angle = vectorLength<T>(v);
			ASSERT(rox::core::IsFinite(angle), "Rotation vector length must be representable in its scalar type.");

			if (angle == T(0.0))
			{
				return {T(0.0), T(0.0), T(0.0), T(1.0)};
			}

			const T sine = rox::core::Sin(angle * T(0.5));
			return canonicalRotation<T>(
			    {(v[0] / angle) * sine, (v[1] / angle) * sine, (v[2] / angle) * sine, rox::core::Cos(angle * T(0.5))});
		}

		template <MathScalar T>
		QuaternionCoefficients<T> interpolateRotation(const QuaternionCoefficients<T>& a, const QuaternionCoefficients<T>& b, T alpha) noexcept
		{
			QuaternionVector<T> delta = rotationLog<T>(multiplyRotation<T>(conjugateRotation<T>(a), b));

			for (T& lane : delta)
			{
				lane *= alpha;
			}

			return canonicalRotation<T>(multiplyRotation<T>(a, rotationExp<T>(delta)));
		}

		template <MathScalar T>
		RotationMatrixRows<T> rotationMatrixRows(const Matrix3x3<T>& m) noexcept
		{
			return {QuaternionVector<T>{m.M[0][0], m.M[0][1], m.M[0][2]},
			        QuaternionVector<T>{m.M[1][0], m.M[1][1], m.M[1][2]},
			        QuaternionVector<T>{m.M[2][0], m.M[2][1], m.M[2][2]}};
		}

		template <MathScalar T>
		bool isRotationMatrix(const Matrix3x3<T>& matrix) noexcept
		{
			const RotationMatrixRows<T> m = rotationMatrixRows<T>(matrix);

			for (rox::core::usize row = 0; row < 3; ++row)
			{
				for (T lane : m[row])
				{
					if (!rox::core::IsFinite(lane))
					{
						return false;
					}
				}

				if (rox::core::Abs(dot<T>(m[row], m[row]) - T(1.0)) > T(1.0e-4))
				{
					return false;
				}

				for (rox::core::usize other = row + 1; other < 3; ++other)
				{
					if (rox::core::Abs(dot<T>(m[row], m[other])) > T(1.0e-4))
					{
						return false;
					}
				}
			}

			return rox::core::Abs(dot<T>(m[0], cross<T>(m[1], m[2])) - T(1.0)) <= T(3.0e-4);
		}

		template <MathScalar T>
		QuaternionCoefficients<T> rotationFromMatrix(const RotationMatrixRows<T>& m) noexcept
		{
			const T trace = m[0][0] + m[1][1] + m[2][2];

			if (trace > T(0.0))
			{
				const T s = T(2.0) * rox::core::Sqrt(trace + T(1.0));
				return canonicalRotation<T>({(m[1][2] - m[2][1]) / s, (m[2][0] - m[0][2]) / s, (m[0][1] - m[1][0]) / s, T(0.25) * s});
			}

			const rox::core::usize i = m[0][0] >= m[1][1] && m[0][0] >= m[2][2] ? 0 : (m[1][1] >= m[2][2] ? 1 : 2);
			const rox::core::usize j = (i + 1) % 3;
			const rox::core::usize k = (i + 2) % 3;
			const T s = T(2.0) * rox::core::Sqrt(T(1.0) + m[i][i] - m[j][j] - m[k][k]);
			QuaternionCoefficients<T> q{};
			q[i] = T(0.25) * s;
			q[j] = (m[i][j] + m[j][i]) / s;
			q[k] = (m[i][k] + m[k][i]) / s;
			q[3] = (m[j][k] - m[k][j]) / s;
			return canonicalRotation<T>(q);
		}
	} // namespace

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::FromToRotation(const Vector3<T>& from, const Vector3<T>& to) noexcept
	{
		ASSERT(validDirection<T>(from) && validDirection<T>(to), "Quaternion direction inputs must be finite and nonzero.");
		const QuaternionVector<T> a = normalizeDirection<T>({from.X, from.Y, from.Z});
		const QuaternionVector<T> b = normalizeDirection<T>({to.X, to.Y, to.Z});
		const QuaternionVector<T> axis = cross<T>(a, b);
		const T sine = vectorLength<T>(axis);
		const T cosine = dot<T>(a, b);

		if (sine == T(0.0))
		{
			if (cosine >= T(0.0))
			{
				return Identity();
			}

			const QuaternionVector<T> perpendicular = perpendicularDirection<T>(normalizeDirection<T>(a));
			return makeRotation<T>({perpendicular[0], perpendicular[1], perpendicular[2], T(0.0)});
		}

		// Use the small complementary angle near a half-turn to retain its real component.
		const T halfAngle = T(0.5) * rox::core::Atan2(sine, rox::core::Abs(cosine));
		const T halfSine = cosine < T(0) ? rox::core::Cos(halfAngle) : rox::core::Sin(halfAngle);
		const T halfCosine = cosine < T(0) ? rox::core::Sin(halfAngle) : rox::core::Cos(halfAngle);
		return makeRotation<T>({axis[0] / sine * halfSine, axis[1] / sine * halfSine, axis[2] / sine * halfSine, halfCosine});
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::FromRotationMatrix(const Matrix3x3<T>& matrix) noexcept
	{
		ASSERT(isRotationMatrix<T>(matrix), "Quaternion matrix input must be a finite proper orthonormal rotation.");
		return makeRotation<T>(rotationFromMatrix<T>(rotationMatrixRows<T>(matrix)));
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::LookRotation(const Vector3<T>& forward, const Vector3<T>& up) noexcept
	{
		ASSERT(validDirection<T>(forward), "Quaternion direction inputs must be finite and nonzero.");
		ASSERT(up.IsFinite(), "Quaternion look up must be finite.");
		const QuaternionVector<T> rawForward{forward.X, forward.Y, forward.Z};
		const QuaternionVector<T> f = normalizeDirection<T>(rawForward);
		QuaternionVector<T> u = cross<T>(f, cross<T>(validDirection<T>(up) ? normalizeDirection<T>({up.X, up.Y, up.Z}) : QuaternionVector<T>{}, f));
		u = vectorLength<T>(u) == T(0.0) ? perpendicularDirection<T>(f) : normalizeDirection<T>(u);
		const QuaternionVector<T> right = normalizeDirection<T>(cross<T>(u, f));
		return makeRotation<T>(rotationFromMatrix<T>({right, cross<T>(f, right), f}));
	}

	template <MathScalar T>
	T Quaternion<T>::AngularDistance(const Quaternion<T>& other) const noexcept
	{
		ASSERT(validRotation<T>(*this) && validRotation<T>(other), "Quaternion rotation inputs must be finite and nonzero.");
		return static_cast<T>(vectorLength<T>(rotationLog<T>(multiplyRotation<T>(conjugateRotation<T>(normalizeRotation<T>(*this)), normalizeRotation<T>(other)))));
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::RotateTowards(const Quaternion<T>& from, const Quaternion<T>& to, T maxRadiansDelta) noexcept
	{
		ASSERT(validRotation<T>(from) && validRotation<T>(to), "Quaternion rotation inputs must be finite and nonzero.");
		ASSERT(rox::core::IsFinite(maxRadiansDelta) && maxRadiansDelta >= T(0.0), "Quaternion angular step must be finite and non-negative.");
		const QuaternionCoefficients<T> a{from.X, from.Y, from.Z, from.W};
		const QuaternionCoefficients<T> b{to.X, to.Y, to.Z, to.W};
		if (maxRadiansDelta == T(0.0))
		{
			return makeRotation<T>(a);
		}

		const T distance = vectorLength<T>(rotationLog<T>(multiplyRotation<T>(conjugateRotation<T>(a), b)));
		return makeRotation<T>(distance <= maxRadiansDelta ? b : interpolateRotation<T>(a, b, maxRadiansDelta / distance));
	}

	template <MathScalar T>
	QuaternionAxisAngle<T> Quaternion<T>::ToAxisAngle() const noexcept
	{
		ASSERT(validRotation<T>(*this), "Quaternion rotation inputs must be finite and nonzero.");
		const QuaternionCoefficients<T> q = normalizeRotation<T>(*this);
		const T sine = rox::core::Hypot(q[0], q[1], q[2]);

		if (sine == T(0.0))
		{
			return {};
		}

		return {{static_cast<T>(q[0] / sine), static_cast<T>(q[1] / sine), static_cast<T>(q[2] / sine)}, static_cast<T>(T(2.0) * rox::core::Atan2(sine, q[3]))};
	}

	template <MathScalar T>
	Vector3<T> Quaternion<T>::ToEulerXYZ() const noexcept
	{
		ASSERT(validRotation<T>(*this), "Quaternion rotation inputs must be finite and nonzero.");
		const QuaternionCoefficients<T> q = normalizeRotation<T>(*this);
		const T x = q[0];
		const T y = q[1];
		const T z = q[2];
		const T w = q[3];
		const T m00 = w * w + x * x - y * y - z * z;
		const T m01 = T(2.0) * (x * y + z * w);
		const T sy = T(2.0) * (y * w - x * z);
		const T cy = rox::core::Hypot(m00, m01);
		const T m11 = w * w - x * x + y * y - z * z;
		Vector3<T> result;

		if (cy > rox::core::Max(T(1.0e-6), T(32) * rox::core::NumericLimits<T>::epsilon()))
		{
			result = {static_cast<T>(rox::core::Atan2(T(2.0) * (y * z + x * w), w * w - x * x - y * y + z * z)),
			          static_cast<T>(rox::core::Atan2(sy, cy)), static_cast<T>(rox::core::Atan2(m01, m00))};
		}
		else
		{
			result = {static_cast<T>(rox::core::Atan2(rox::core::CopySign(T(1.0), sy) * T(2.0) * (x * y - z * w), m11)),
			          static_cast<T>(rox::core::CopySign(rox::core::PI<T> * T(0.5), sy)), T(0.0)};
		}

		if (result.X == T(0.0))
		{
			result.X = T(0.0);
		}

		if (result.Y == T(0.0))
		{
			result.Y = T(0.0);
		}

		if (result.Z == T(0.0))
		{
			result.Z = T(0.0);
		}

		return result;
	}

	template <MathScalar T>
	QuaternionSwingTwist<T> Quaternion<T>::DecomposeSwingTwist(const Vector3<T>& twistAxis) const noexcept
	{
		ASSERT(validRotation<T>(*this), "Quaternion rotation inputs must be finite and nonzero.");
		ASSERT(validDirection<T>(twistAxis), "Quaternion direction inputs must be finite and nonzero.");
		const QuaternionCoefficients<T> q = normalizeRotation<T>(*this);
		const QuaternionVector<T> rawAxis{twistAxis.X, twistAxis.Y, twistAxis.Z};
		const QuaternionVector<T> axis = normalizeDirection<T>(rawAxis);
		int qExponent = 0;
		int axisExponent = 0;
		std::frexp(rox::core::Max({rox::core::Abs(X), rox::core::Abs(Y), rox::core::Abs(Z), rox::core::Abs(W)}), &qExponent);
		std::frexp(rox::core::Max({rox::core::Abs(rawAxis[0]), rox::core::Abs(rawAxis[1]), rox::core::Abs(rawAxis[2])}), &axisExponent);
		const QuaternionVector<T> scaledAxis{std::scalbn(rawAxis[0], -axisExponent),
		                                     std::scalbn(rawAxis[1], -axisExponent),
		                                     std::scalbn(rawAxis[2], -axisExponent)};
		const QuaternionVector<T> scaledVector{std::scalbn(X, -qExponent), 
											   std::scalbn(Y, -qExponent),
		                                       std::scalbn(Z, -qExponent)};
		const T projection = dot<T>(scaledVector, scaledAxis) / vectorLength<T>(scaledAxis);
		const T real = std::scalbn(W, -qExponent);
		if (projection == T(0.0) && q[3] == T(0.0))
		{
			return {makeRotation<T>(q), Identity()};
		}

		const QuaternionCoefficients<T> twist = canonicalRotation<T>({axis[0] * projection, axis[1] * projection, axis[2] * projection, real});
		return {makeRotation<T>(multiplyRotation<T>(q, conjugateRotation<T>(twist))), makeRotation<T>(twist)};
	}

	template <MathScalar T>
	Vector3<T> Quaternion<T>::RotationLog() const noexcept
	{
		ASSERT(validRotation<T>(*this), "Quaternion rotation inputs must be finite and nonzero.");
		const QuaternionVector<T> logarithm = rotationLog<T>(normalizeRotation<T>(*this));
		return {static_cast<T>(logarithm[0]), static_cast<T>(logarithm[1]), static_cast<T>(logarithm[2])};
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::RotationExp(const Vector3<T>& rotationVector) noexcept
	{
		ASSERT(rotationVector.IsFinite(), "Quaternion rotation vector must be finite.");
		return makeRotation<T>(rotationExp<T>({rotationVector.X, rotationVector.Y, rotationVector.Z}));
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::SquadTangent(const Quaternion<T>& previous, const Quaternion<T>& current, const Quaternion<T>& next) noexcept
	{
		ASSERT(validRotation<T>(previous) && validRotation<T>(current) && validRotation<T>(next), "Quaternion rotation inputs must be finite and nonzero.");
		const QuaternionCoefficients<T> p{previous.X, previous.Y, previous.Z, previous.W};
		const QuaternionCoefficients<T> c{current.X, current.Y, current.Z, current.W};
		const QuaternionCoefficients<T> n{next.X, next.Y, next.Z, next.W};
		const QuaternionVector<T> before = rotationLog<T>(multiplyRotation<T>(conjugateRotation<T>(c), p));
		const QuaternionVector<T> after = rotationLog<T>(multiplyRotation<T>(conjugateRotation<T>(c), n));
		QuaternionVector<T> delta{};
		for (rox::core::usize index = 0; index < 3; ++index)
		{
			delta[index] = -T(0.25) * (before[index] + after[index]);
		}

		return makeRotation<T>(multiplyRotation<T>(c, rotationExp<T>(delta)));
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::Squad(const Quaternion<T>& from, const Quaternion<T>& to, const Quaternion<T>& fromTangent, const Quaternion<T>& toTangent, T alpha) noexcept
	{
		ASSERT(validRotation<T>(from) && validRotation<T>(to) && validRotation<T>(fromTangent) && validRotation<T>(toTangent), "Quaternion rotation inputs must be finite and nonzero.");
		ASSERT(rox::core::IsFinite(alpha) && alpha >= T(0.0) && alpha <= T(1.0), "Quaternion interpolation alpha must be finite and within [0, 1].");
		const QuaternionCoefficients<T> a{from.X, from.Y, from.Z, from.W};
		const QuaternionCoefficients<T> b{to.X, to.Y, to.Z, to.W};
		const QuaternionCoefficients<T> c{fromTangent.X, fromTangent.Y, fromTangent.Z, fromTangent.W};
		const QuaternionCoefficients<T> d{toTangent.X, toTangent.Y, toTangent.Z, toTangent.W};
		if (alpha == T(0.0))
		{
			return makeRotation<T>(a);
		}

		if (alpha == T(1.0))
		{
			return makeRotation<T>(b);
		}

		return makeRotation<T>(interpolateRotation<T>(interpolateRotation<T>(a, b, alpha),
		                                              interpolateRotation<T>(c, d, alpha),
		                                              T(2.0) * alpha * (T(1.0) - alpha)));
	}

	template <MathScalar T>
	T Quaternion<T>::Length() const noexcept
	{
		const T xyLength = rox::core::Hypot(static_cast<T>(X), static_cast<T>(Y));
		const T zwLength = rox::core::Hypot(static_cast<T>(Z), static_cast<T>(W));
		return static_cast<T>(rox::core::Hypot(xyLength, zwLength));
	}

	template <MathScalar T>
	bool Quaternion<T>::TryNormalize(Quaternion<T>* outNormalized, T epsilon) const noexcept
	{
		ASSERT(outNormalized != nullptr, "Quaternion output must not be null.");
		ASSERT(rox::core::IsFinite(epsilon) && epsilon >= T(0), "Quaternion epsilon must be finite and non-negative.");
		if (!IsFinite())
		{
			return false;
		}

		const T scale = rox::core::Max({rox::core::Abs(X), rox::core::Abs(Y), rox::core::Abs(Z), rox::core::Abs(W)});
		if (scale == T(0))
		{
			return false;
		}

		const Quaternion<T> q{X / scale, Y / scale, Z / scale, W / scale};
		const T length = rox::core::Hypot(rox::core::Hypot(q.X, q.Y), rox::core::Hypot(q.Z, q.W));
		if (scale <= epsilon / length)
		{
			return false;
		}

		const Quaternion<T> result{q.X / length, q.Y / length, q.Z / length, q.W / length};
		if (!result.IsFinite())
		{
			return false;
		}

		*outNormalized = result;
		return true;
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::Normalized(T epsilon) const noexcept
	{
		Quaternion<T> result;
		const bool bNormalized = TryNormalize(&result, epsilon);
		ASSERT(bNormalized, "Quaternion must be finite and longer than the normalization epsilon.");
		return result;
	}

	template <MathScalar T>
	bool Quaternion<T>::TryInverse(Quaternion<T>* outInverse, T epsilon) const noexcept
	{
		ASSERT(outInverse != nullptr, "Quaternion output must not be null.");
		ASSERT(rox::core::IsFinite(epsilon) && epsilon >= T(0), "Quaternion epsilon must be finite and non-negative.");
		if (!IsFinite())
		{
			return false;
		}

		const T scale = rox::core::Max({rox::core::Abs(X), rox::core::Abs(Y), rox::core::Abs(Z), rox::core::Abs(W)});
		if (scale == T(0))
		{
			return false;
		}

		const Quaternion<T> q{X / scale, Y / scale, Z / scale, W / scale};
		const T length = rox::core::Hypot(rox::core::Hypot(q.X, q.Y), rox::core::Hypot(q.Z, q.W));
		if (scale <= epsilon / length)
		{
			return false;
		}

		const T divisor = length * length;
		const Quaternion<T> result{(-q.X / divisor) / scale, (-q.Y / divisor) / scale, (-q.Z / divisor) / scale,
		                           (q.W / divisor) / scale};
		if (!result.IsFinite())
		{
			return false;
		}

		*outInverse = result;
		return true;
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::Inversed(T epsilon) const noexcept
	{
		Quaternion<T> result;
		const bool bInverted = TryInverse(&result, epsilon);
		ASSERT(bInverted, "Quaternion must be finite and invertible.");
		return result;
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::FromAxisAngle(const Vector3<T>& axis, T radians) noexcept
	{
		const Vector3<T> normalizedAxis = axis.Normalized();
		const T halfAngle = radians * T(0.5);
		const T sine = rox::core::Sin(halfAngle);
		return {normalizedAxis.X * sine, normalizedAxis.Y * sine, normalizedAxis.Z * sine, rox::core::Cos(halfAngle)};
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::FromEulerXYZ(const Vector3<T>& radians) noexcept
	{
		const Quaternion<T> x = FromAxisAngle(Vector3<T>::UnitX(), radians.X);
		const Quaternion<T> y = FromAxisAngle(Vector3<T>::UnitY(), radians.Y);
		const Quaternion<T> z = FromAxisAngle(Vector3<T>::UnitZ(), radians.Z);
		return (z * y * x).Normalized();
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::Nlerp(const Quaternion<T>& from, const Quaternion<T>& to, T alpha) noexcept
	{
		ASSERT(rox::core::IsFinite(alpha), "Quaternion interpolation alpha must be finite.");
		const Quaternion<T> normalizedFrom = from.Normalized();
		Quaternion<T> normalizedTo = to.Normalized();

		if (normalizedFrom.Dot(normalizedTo) < T(0.0))
		{
			normalizedTo = -normalizedTo;
		}

		return (normalizedFrom * (T(1.0) - alpha) + normalizedTo * alpha).Normalized();
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::Slerp(const Quaternion<T>& from, const Quaternion<T>& to, T alpha) noexcept
	{
		ASSERT(rox::core::IsFinite(alpha), "Quaternion interpolation alpha must be finite.");
		const Quaternion<T> normalizedFrom = from.Normalized();
		Quaternion<T> normalizedTo = to.Normalized();
		T cosine = normalizedFrom.Dot(normalizedTo);

		if (cosine < T(0.0))
		{
			normalizedTo = -normalizedTo;
			cosine = -cosine;
		}

		cosine = rox::core::ClampValue(cosine, -T(1.0), T(1.0));

		if (cosine > T(0.9995))
		{
			return (normalizedFrom * (T(1) - alpha) + normalizedTo * alpha).Normalized();
		}

		const T angle = rox::core::Acos(cosine);
		const T inverseSine = T(1.0) / rox::core::Sin(angle);
		return (normalizedFrom * rox::core::Sin((T(1.0) - alpha) * angle) +
		        normalizedTo * rox::core::Sin(alpha * angle)) *
		       inverseSine;
	}

	template <MathScalar T>
	Vector3<T> Quaternion<T>::RotateVector(const Vector3<T>& vector) const noexcept
	{
		const Quaternion<T> rotation = Normalized();
		const Vector3<T> quaternionVector{rotation.X, rotation.Y, rotation.Z};
		const Vector3<T> twiceCross = T(2.0) * quaternionVector.Cross(vector);
		return vector + rotation.W * twiceCross + quaternionVector.Cross(twiceCross);
	}

	template <MathScalar T>
	Matrix3x3<T> Quaternion<T>::ToMatrix3x3() const noexcept
	{
		const Quaternion<T> q = Normalized();
		const T xx = q.X * q.X;
		const T yy = q.Y * q.Y;
		const T zz = q.Z * q.Z;
		const T xy = q.X * q.Y;
		const T xz = q.X * q.Z;
		const T yz = q.Y * q.Z;
		const T wx = q.W * q.X;
		const T wy = q.W * q.Y;
		const T wz = q.W * q.Z;

		return {T(1.0) - T(2.0) * (yy + zz), T(2.0) * (xy + wz),          T(2.0) * (xz - wy),
		        T(2.0) * (xy - wz),          T(1.0) - T(2.0) * (xx + zz), T(2.0) * (yz + wx),
		        T(2.0) * (xz + wy),          T(2.0) * (yz - wx),          T(1.0) - T(2.0) * (xx + yy)};
	}

	template <MathScalar T>
	Matrix4x4<T> Quaternion<T>::ToMatrix4x4() const noexcept
	{
		const Matrix3x3<T> matrix = ToMatrix3x3();
		return {matrix.M[0][0], matrix.M[0][1], matrix.M[0][2], T(0.0),         matrix.M[1][0], matrix.M[1][1],
		        matrix.M[1][2], T(0.0),         matrix.M[2][0], matrix.M[2][1], matrix.M[2][2], T(0.0),
		        T(0.0),         T(0.0),         T(0.0),         T(1.0)};
	}

	template <MathScalar T>
	Quaternion<T> Quaternion<T>::operator*(const Quaternion<T>& other) const noexcept
	{
		return {W * other.X + X * other.W + Y * other.Z - Z * other.Y,
		        W * other.Y - X * other.Z + Y * other.W + Z * other.X,
		        W * other.Z + X * other.Y - Y * other.X + Z * other.W,
		        W * other.W - X * other.X - Y * other.Y - Z * other.Z};
	}

	template struct Quaternion<float32>;
	template struct Quaternion<float64>;
} // namespace rox::math
