#pragma once

#include <MathScalar.hpp>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <ProjectionMath.h>

#include <cmath>
#include <limits>

namespace rox::math::detail
{
	template <MathScalar T>
	struct ProjectionVector3
	{
		T X;
		T Y;
		T Z;
	};

	template <MathScalar T>
	struct ProjectionVector4
	{
		T X;
		T Y;
		T Z;
		T W;
	};

	template <MathScalar T>
	inline ProjectionVector4<T> ProjectionTransform(const Matrix4x4<T>& m, const ProjectionVector3<T>& p) noexcept
	{
		return {p.X * m[0][0] + p.Y * m[1][0] + p.Z * m[2][0] + m[3][0],
		        p.X * m[0][1] + p.Y * m[1][1] + p.Z * m[2][1] + m[3][1],
		        p.X * m[0][2] + p.Y * m[1][2] + p.Z * m[2][2] + m[3][2],
		        p.X * m[0][3] + p.Y * m[1][3] + p.Z * m[2][3] + m[3][3]};
	}

	template <MathScalar T>
	inline bool ProjectionDivide(const ProjectionVector4<T>& p, ProjectionVector3<T>& out) noexcept
	{
		if (p.W == 0)
		{
			return false;
		}

		const ProjectionVector3<T> value{p.X / p.W, p.Y / p.W, p.Z / p.W};
		if (!core::IsFinite(value.X) || !core::IsFinite(value.Y) || !core::IsFinite(value.Z))
		{
			return false;
		}

		out = value;
		return true;
	}

	template <MathScalar T>
	inline bool ProjectionNarrow(const ProjectionVector3<T>& p, Vector3<T>& out) noexcept
	{
		constexpr T MAXIMUM = core::NumericLimits<T>::max();

		if (!core::IsFinite(p.X) || !core::IsFinite(p.Y) || !core::IsFinite(p.Z) ||
		    core::Abs(p.X) > MAXIMUM || core::Abs(p.Y) > MAXIMUM || core::Abs(p.Z) > MAXIMUM)
		{
			return false;
		}

		out = {static_cast<T>(p.X), static_cast<T>(p.Y), static_cast<T>(p.Z)};
		return true;
	}
} // namespace rox::math::detail
