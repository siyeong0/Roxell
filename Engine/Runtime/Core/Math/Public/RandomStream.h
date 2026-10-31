#pragma once

#include <MathScalar.hpp>

#include <Types.h>

namespace rox::math
{
	struct RandomStreamState final
	{
		uint64 State = 0;
		uint64 Increment = 1;

		[[nodiscard]] bool operator==(const RandomStreamState&) const noexcept = default;
	};

	// Owns an allocation-free PCG32 stream. Concurrent access requires caller synchronization.
	class RandomStream final
	{
	public:
		RandomStream(uint64 seed, uint64 streamSelector) noexcept;

		// The selector uses its low 63 bits; Reset reproduces construction exactly.
		void Reset(uint64 seed, uint64 streamSelector) noexcept;
		[[nodiscard]] RandomStreamState GetState() const noexcept;

		// Snapshots must have an odd Increment. This is a numeric state, not a wire format.
		void RestoreState(RandomStreamState state) noexcept;
		[[nodiscard]] uint32 NextU32() noexcept;

		// The first draw supplies the high half, the second the low half.
		[[nodiscard]] uint64 NextU64() noexcept;

		// Integer ranges are half-open and require minimum < maximum. Rejection may consume extra draws.
		[[nodiscard]] uint32 NextU32(uint32 minimum, uint32 maximum) noexcept;
		[[nodiscard]] uint64 NextU64(uint64 minimum, uint64 maximum) noexcept;
		[[nodiscard]] int32 NextI32(int32 minimum, int32 maximum) noexcept;
		[[nodiscard]] int64 NextI64(int64 minimum, int64 maximum) noexcept;

		// Draws a multiple of 2^-24 in [0,1), using the high 24 bits of one draw.
		[[nodiscard]] float32 Float01() noexcept;

		// Requires finite minimum < maximum. Rounding never includes the upper endpoint.
		[[nodiscard]] float32 Float(float32 minimum, float32 maximum) noexcept;

		// Requires finite probability in [0,1]. Both endpoints still consume one Float01 draw.
		[[nodiscard]] bool Bool(float32 probability = 0.5f) noexcept;

		// Float uses one draw (24 bits); double uses two draws (53 bits). Both exclude one.
		template <MathScalar T = float32>
		[[nodiscard]] T Real01() noexcept;

		template <MathScalar T = float32>
		[[nodiscard]] T Real(T minimum, T maximum) noexcept;

	private:
		RandomStreamState m_State;
	};
} // namespace rox::math
