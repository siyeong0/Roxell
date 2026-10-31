#include <GeometryQueries.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>

namespace rox::math
{
	namespace
	{
		// Error-free sums retain low terms through cancellation without widening the scalar type.
		template <MathScalar T>
		bool addExpansion(T value, T* pieces, uint32& count) noexcept
		{
			uint32 retained = 0;
			for (uint32 index = 0; index < count; ++index)
			{
				const T other = pieces[index];
				const T sum = value + other;
				if (!core::IsFinite(sum))
				{
					return false;
				}

				const T virtualOther = sum - value;
				const T error = (value - (sum - virtualOther)) + (other - virtualOther);
				if (!core::IsFinite(error))
				{
					return false;
				}

				if (error != T(0))
				{
					pieces[retained++] = error;
				}

				value = sum;
			}

			if (value != T(0) || retained == 0)
			{
				pieces[retained++] = value;
			}

			count = retained;
			return true;
		}

		template <MathScalar T>
		core::Optional<core::Pair<T, T>> transformedCoordinate(const T* point, const AffineTransform<T>& transform, uint32 axis) noexcept
		{
			T pieces[8]{};
			uint32 count = 0;
			uint32 underflows = 0;
			T evaluated = T(0);
			for (uint32 row = 0; row < 3; ++row)
			{
				const T coefficient = transform.Linear[row][axis];
				const T product = point[row] * coefficient;
				if (!core::IsFinite(product))
				{
					return {};
				}

				evaluated += product;
				const T error = core::Fma(point[row], coefficient, -product);
				if (!addExpansion(error, pieces, count) || !addExpansion(product, pieces, count))
				{
					return {};
				}

				// Residuals below the subnormal range cannot be represented by the expansion.
				if (point[row] != T(0) && coefficient != T(0) && core::Abs(product) < core::NumericLimits<T>::min() / core::NumericLimits<T>::epsilon())
				{
					++underflows;
				}
			}

			const T translation[] = {transform.Translation.X, transform.Translation.Y, transform.Translation.Z};
			if (!addExpansion(translation[axis], pieces, count))
			{
				return {};
			}

			T lower = pieces[count - 1];
			T upper = lower;
			const T residual = count > 1 ? pieces[count - 2] : T(0);
			const T infinity = core::NumericLimits<T>::infinity();
			if (residual < T(0))
			{
				lower = core::NextAfter(lower, -infinity);
			}

			if (residual > T(0))
			{
				upper = core::NextAfter(upper, infinity);
			}

			for (uint32 index = 0; index < underflows; ++index)
			{
				lower = core::NextAfter(lower, -infinity);
				upper = core::NextAfter(upper, infinity);
			}

			evaluated += translation[axis];
			if (!core::IsFinite(evaluated))
			{
				return {};
			}

			lower = core::Min(lower, evaluated);
			upper = core::Max(upper, evaluated);
			if (!core::IsFinite(lower) || !core::IsFinite(upper))
			{
				return {};
			}

			return core::Pair<T, T>{lower, upper};
		}
	} // namespace

	template <MathScalar T>
	core::Optional<Box<T>> TransformedBounds(const Box<T>& box, const AffineTransform<T>& transform) noexcept
	{
		if (!box.IsValid() || !transform.IsFinite())
		{
			return {};
		}

		T lower[] = {core::NumericLimits<T>::infinity(), core::NumericLimits<T>::infinity(), core::NumericLimits<T>::infinity()};
		T upper[] = {-lower[0], -lower[1], -lower[2]};
		for (uint32 corner = 0; corner < 8; ++corner)
		{
			const T point[] = {(corner & 1) ? box.Max.X : box.Min.X, (corner & 2) ? box.Max.Y : box.Min.Y, (corner & 4) ? box.Max.Z : box.Min.Z};
			for (uint32 axis = 0; axis < 3; ++axis)
			{
				const core::Optional<core::Pair<T, T>> coordinate = transformedCoordinate(point, transform, axis);
				if (!coordinate)
				{
					return {};
				}

				lower[axis] = core::Min(lower[axis], coordinate->first);
				upper[axis] = core::Max(upper[axis], coordinate->second);
			}
		}

		return Box<T>{{lower[0], lower[1], lower[2]}, {upper[0], upper[1], upper[2]}};
	}

	template core::Optional<Box<float32>> TransformedBounds(const Box<float32>&, const AffineTransform<float32>&) noexcept;
	template core::Optional<Box<float64>> TransformedBounds(const Box<float64>&, const AffineTransform<float64>&) noexcept;
} // namespace rox::math
