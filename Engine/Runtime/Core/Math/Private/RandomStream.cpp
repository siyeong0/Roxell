#include <RandomStream.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <Assertion.h>

#include <bit>
#include <cmath>
#include <limits>

namespace rox::math
{
	RandomStream::RandomStream(uint64 seed, uint64 streamSelector) noexcept
	{
		Reset(seed, streamSelector);
	}

	void RandomStream::Reset(uint64 seed, uint64 streamSelector) noexcept
	{
		m_State.State = 0;
		m_State.Increment = (streamSelector << 1) | 1;
		(void)NextU32();
		m_State.State += seed;
		(void)NextU32();
	}

	RandomStreamState RandomStream::GetState() const noexcept
	{
		return m_State;
	}

	void RandomStream::RestoreState(RandomStreamState state) noexcept
	{
		ASSERT((state.Increment & 1) != 0, "Random stream state requires an odd increment");
		m_State = state;
	}

	uint32 RandomStream::NextU32() noexcept
	{
		const uint64 previousState = m_State.State;
		m_State.State = previousState * 6364136223846793005ull + m_State.Increment;
		const uint32 mixed = static_cast<uint32>(((previousState >> 18) ^ previousState) >> 27);
		return rox::core::RotateRight(mixed, static_cast<int>(previousState >> 59));
	}

	uint64 RandomStream::NextU64() noexcept
	{
		const uint64 high = NextU32();
		return (high << 32) | NextU32();
	}

	uint32 RandomStream::NextU32(uint32 minimum, uint32 maximum) noexcept
	{
		ASSERT(minimum < maximum, "Random integer range requires minimum below maximum");
		const uint32 width = maximum - minimum;
		const uint32 threshold = (uint32{0} - width) % width;
		for (;;)
		{
			const uint32 value = NextU32();

			if (value >= threshold)
			{
				return minimum + value % width;
			}
		}
	}

	uint64 RandomStream::NextU64(uint64 minimum, uint64 maximum) noexcept
	{
		ASSERT(minimum < maximum, "Random integer range requires minimum below maximum");
		const uint64 width = maximum - minimum;
		const uint64 threshold = (uint64{0} - width) % width;
		for (;;)
		{
			const uint64 value = NextU64();

			if (value >= threshold)
			{
				return minimum + value % width;
			}
		}
	}

	int32 RandomStream::NextI32(int32 minimum, int32 maximum) noexcept
	{
		ASSERT(minimum < maximum, "Random integer range requires minimum below maximum");
		constexpr uint32 SIGN_BIT = uint32{1} << 31;
		const uint32 lower = rox::core::BitCast<uint32>(minimum) ^ SIGN_BIT;
		const uint32 upper = rox::core::BitCast<uint32>(maximum) ^ SIGN_BIT;
		return rox::core::BitCast<int32>(NextU32(lower, upper) ^ SIGN_BIT);
	}

	int64 RandomStream::NextI64(int64 minimum, int64 maximum) noexcept
	{
		ASSERT(minimum < maximum, "Random integer range requires minimum below maximum");
		constexpr uint64 SIGN_BIT = uint64{1} << 63;
		const uint64 lower = rox::core::BitCast<uint64>(minimum) ^ SIGN_BIT;
		const uint64 upper = rox::core::BitCast<uint64>(maximum) ^ SIGN_BIT;
		return rox::core::BitCast<int64>(NextU64(lower, upper) ^ SIGN_BIT);
	}

	float32 RandomStream::Float01() noexcept
	{
		static_assert(rox::core::NumericLimits<float32>::is_iec559 && rox::core::NumericLimits<float32>::digits == 24);
		return static_cast<float32>(NextU32() >> 8) * 0x1p-24f;
	}

	float32 RandomStream::Float(float32 minimum, float32 maximum) noexcept
	{
		return Real<float32>(minimum, maximum);
	}

	bool RandomStream::Bool(float32 probability) noexcept
	{
		ASSERT(rox::core::IsFinite(probability) && probability >= 0.0f && probability <= 1.0f,
		       "Random probability requires a finite value between zero and one");
		return Float01() < probability;
	}

	template <MathScalar T>
	T RandomStream::Real01() noexcept
	{
		if constexpr (rox::core::IsSame<T, float32>)
		{
			return Float01();
		}
		else
		{
			return static_cast<T>(NextU64() >> 11) * T(0x1p-53);
		}
	}

	template <MathScalar T>
	T RandomStream::Real(T minimum, T maximum) noexcept
	{
		ASSERT(rox::core::IsFinite(minimum) && rox::core::IsFinite(maximum) && minimum < maximum, "Random range requires finite ordered endpoints.");
		const T value = rox::core::LerpValue(minimum, maximum, Real01<T>());
		return value < maximum ? value : rox::core::NextAfter(maximum, minimum);
	}

	template float32 RandomStream::Real01<float32>() noexcept;
	template float64 RandomStream::Real01<float64>() noexcept;
	template float32 RandomStream::Real(float32, float32) noexcept;
	template float64 RandomStream::Real(float64, float64) noexcept;
} // namespace rox::math
