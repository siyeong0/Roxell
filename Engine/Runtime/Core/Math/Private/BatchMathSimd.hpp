#pragma once

#include <MathScalar.hpp>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#elif defined(_M_ARM64) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace rox::math
{
	namespace
	{
#if defined(_M_X64) || defined(__SSE2__)
		template <MathScalar T>
		struct BatchPair;
		template <>
		struct BatchPair<float32>
		{
			__m128 Value;
		};

		template <>
		struct BatchPair<float64>
		{
			__m128d Value;
		};

		template <MathScalar T>
		BatchPair<T> pair(T first, T second) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				return {_mm_set_ps(0, 0, second, first)};
			}
			else
			{
				return {_mm_set_pd(second, first)};
			}
		}

		template <MathScalar T>
		BatchPair<T> broadcast(T value) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				return {_mm_set1_ps(value)};
			}
			else
			{
				return {_mm_set1_pd(value)};
			}
		}

		template <MathScalar T>
		BatchPair<T> add(BatchPair<T> a, BatchPair<T> b) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				return {_mm_add_ps(a.Value, b.Value)};
			}
			else
			{
				return {_mm_add_pd(a.Value, b.Value)};
			}
		}

		template <MathScalar T>
		BatchPair<T> multiply(BatchPair<T> a, BatchPair<T> b) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				return {_mm_mul_ps(a.Value, b.Value)};
			}
			else
			{
				return {_mm_mul_pd(a.Value, b.Value)};
			}
		}

		template <MathScalar T>
		void store(T* out, BatchPair<T> value) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				out[0] = _mm_cvtss_f32(value.Value);
				out[1] = _mm_cvtss_f32(_mm_shuffle_ps(value.Value, value.Value, _MM_SHUFFLE(1, 1, 1, 1)));
			}
			else
			{
				_mm_storeu_pd(out, value.Value);
			}
		}
#elif defined(_M_ARM64) || defined(__aarch64__)
		template <MathScalar T>
		struct BatchPair;
		template <>
		struct BatchPair<float32>
		{
			float32x2_t Value;
		};

		template <>
		struct BatchPair<float64>
		{
			float64x2_t Value;
		};

		template <MathScalar T>
		BatchPair<T> pair(T first, T second) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				return {vset_lane_f32(second, vdup_n_f32(first), 1)};
			}
			else
			{
				return {vsetq_lane_f64(second, vdupq_n_f64(first), 1)};
			}
		}

		template <MathScalar T>
		BatchPair<T> broadcast(T value) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				return {vdup_n_f32(value)};
			}
			else
			{
				return {vdupq_n_f64(value)};
			}
		}

		template <MathScalar T>
		BatchPair<T> add(BatchPair<T> a, BatchPair<T> b) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				return {vadd_f32(a.Value, b.Value)};
			}
			else
			{
				return {vaddq_f64(a.Value, b.Value)};
			}
		}

		template <MathScalar T>
		BatchPair<T> multiply(BatchPair<T> a, BatchPair<T> b) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				return {vmul_f32(a.Value, b.Value)};
			}
			else
			{
				return {vmulq_f64(a.Value, b.Value)};
			}
		}

		template <MathScalar T>
		void store(T* out, BatchPair<T> value) noexcept
		{
			if constexpr (core::IsSame<T, float32>)
			{
				vst1_f32(out, value.Value);
			}
			else
			{
				vst1q_f64(out, value.Value);
			}
		}
#else
		template <MathScalar T>
		struct BatchPair
		{
			T First;
			T Second;
		};

		template <MathScalar T>
		BatchPair<T> pair(T first, T second) noexcept
		{
			return {first, second};
		}

		template <MathScalar T>
		BatchPair<T> broadcast(T value) noexcept
		{
			return {value, value};
		}

		template <MathScalar T>
		BatchPair<T> add(BatchPair<T> a, BatchPair<T> b) noexcept
		{
			return {a.First + b.First, a.Second + b.Second};
		}

		template <MathScalar T>
		BatchPair<T> multiply(BatchPair<T> a, BatchPair<T> b) noexcept
		{
			return {a.First * b.First, a.Second * b.Second};
		}

		template <MathScalar T>
		void store(T* out, BatchPair<T> value) noexcept
		{
			out[0] = value.First;
			out[1] = value.Second;
		}
#endif
	} // namespace
} // namespace rox::math
