#include <RandomNoise.h>
#include <Assertion.h>
#include <Numeric.hpp>

#include <cmath>

namespace rox::math
{
	namespace
	{
		uint64 absorbRandomWord(uint64 state, uint64 word) noexcept
		{
			state ^= word;
			state += 0x9e3779b97f4a7c15ull;
			state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ull;
			state = (state ^ (state >> 27)) * 0x94d049bb133111ebull;
			return state ^ (state >> 31);
		}

		template <MathScalar T>
		T fade(T t) noexcept
		{
			return t * t * t * (t * (t * 6 - 15) + 10);
		}

		template <MathScalar T>
		T noiseCorner(uint64 seed, int64 x, int64 y, int64 z, bool threeDimensions) noexcept
		{
			uint64 value = absorbRandomWord(threeDimensions ? 0x4368724e6f693033ull : 0x4368724e6f693032ull, seed);
			value = absorbRandomWord(value, static_cast<uint64>(x));
			value = absorbRandomWord(value, static_cast<uint64>(y));

			if (threeDimensions)
			{
				value = absorbRandomWord(value, static_cast<uint64>(z));
			}

			if constexpr (core::IsSame<T, float32>)
			{
				return static_cast<T>(value >> 40) * T(0x1p-24);
			}
			else
			{
				return static_cast<T>(value >> 11) * T(0x1p-53);
			}
		}
	} // namespace

	uint64 RandomCounterU64(uint64 seed, uint64 counter, uint64 streamSelector) noexcept
	{
		uint64 state = absorbRandomWord(0x436872526e673031ull, seed);
		state = absorbRandomWord(state, streamSelector);
		return absorbRandomWord(state, counter);
	}

	template <MathScalar T>
	T RandomCounterFloat01(uint64 seed, uint64 counter, uint64 streamSelector) noexcept
	{
		const uint64 value = RandomCounterU64(seed, counter, streamSelector);
		if constexpr (core::IsSame<T, float32>)
		{
			return static_cast<T>(value >> 40) * T(0x1p-24);
		}
		else
		{
			return static_cast<T>(value >> 11) * T(0x1p-53);
		}
	}

	template <MathScalar T>
	T ValueNoise2D(const Vector2<T>& position, uint64 seed) noexcept
	{
		ASSERT(position.IsFinite() && core::Abs(position.X) <= 1048576 && core::Abs(position.Y) <= 1048576, "Random noise coordinates must be finite and within the supported range.");
		const int64 x = static_cast<int64>(core::Floor(position.X));
		const int64 y = static_cast<int64>(core::Floor(position.Y));
		const T u = fade<T>(static_cast<T>(position.X) - x);
		const T v = fade<T>(static_cast<T>(position.Y) - y);
		const T a = noiseCorner<T>(seed, x, y, 0, false);
		const T b = noiseCorner<T>(seed, x + 1, y, 0, false);
		const T c = noiseCorner<T>(seed, x, y + 1, 0, false);
		const T d = noiseCorner<T>(seed, x + 1, y + 1, 0, false);
		return static_cast<T>((a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v);
	}

	template <MathScalar T>
	T ValueNoise3D(const Vector3<T>& position, uint64 seed) noexcept
	{
		ASSERT(position.IsFinite() && core::Abs(position.X) <= 1048576 && core::Abs(position.Y) <= 1048576 && core::Abs(position.Z) <= 1048576,
		       "Random noise coordinates must be finite and within the supported range.");
		const int64 x = static_cast<int64>(core::Floor(position.X));
		const int64 y = static_cast<int64>(core::Floor(position.Y));
		const int64 z = static_cast<int64>(core::Floor(position.Z));
		const T u = fade<T>(static_cast<T>(position.X) - x);
		const T v = fade<T>(static_cast<T>(position.Y) - y);
		const T w = fade<T>(static_cast<T>(position.Z) - z);
		T slices[2]{};
		for (int64 dz = 0; dz < 2; ++dz)
		{
			const T a = noiseCorner<T>(seed, x, y, z + dz, true);
			const T b = noiseCorner<T>(seed, x + 1, y, z + dz, true);
			const T c = noiseCorner<T>(seed, x, y + 1, z + dz, true);
			const T d = noiseCorner<T>(seed, x + 1, y + 1, z + dz, true);
			slices[dz] = core::LerpValue(core::LerpValue(a, b, u), core::LerpValue(c, d, u), v);
		}

		return core::LerpValue(slices[0], slices[1], w);
	}

	template float32 RandomCounterFloat01(uint64 seed, uint64 counter, uint64 streamSelector) noexcept;
	template float64 RandomCounterFloat01(uint64 seed, uint64 counter, uint64 streamSelector) noexcept;
	template float32 ValueNoise2D(const Vector2<float32>& position, uint64 seed) noexcept;
	template float64 ValueNoise2D(const Vector2<float64>& position, uint64 seed) noexcept;
	template float32 ValueNoise3D(const Vector3<float32>& position, uint64 seed) noexcept;
	template float64 ValueNoise3D(const Vector3<float64>& position, uint64 seed) noexcept;
} // namespace rox::math
