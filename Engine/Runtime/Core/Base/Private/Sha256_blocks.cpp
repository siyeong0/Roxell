#include "Sha256_blocks.h"
#include <Vocabulary.hpp>
#include <Types.h>
#include <Utility.hpp>
#include <Assertion.h>
#include <bit>

namespace rox::core
{
	namespace
	{
		void processBlock(const uint8* block, StaticArray<uint32, 8>& state) noexcept
		{
			StaticArray<uint32, 64> words = {};

			for (uint32 index = 0; index < 16; ++index)
			{
				const uint32 offset = index * 4;
				words[index] = (static_cast<uint32>(block[offset]) << 24) |
							   (static_cast<uint32>(block[offset + 1]) << 16) |
							   (static_cast<uint32>(block[offset + 2]) << 8) | static_cast<uint32>(block[offset + 3]);
			}

			for (uint32 index = 16; index < 64; ++index)
			{
				const uint32 sigma0 =
					core::RotateRight(words[index - 15], 7) ^ core::RotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3);
				const uint32 sigma1 =
					core::RotateRight(words[index - 2], 17) ^ core::RotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10);
				words[index] = words[index - 16] + sigma0 + words[index - 7] + sigma1;
			}

			uint32 a = state[0];
			uint32 b = state[1];
			uint32 c = state[2];
			uint32 d = state[3];
			uint32 e = state[4];
			uint32 f = state[5];
			uint32 g = state[6];
			uint32 h = state[7];

			for (uint32 index = 0; index < 64; ++index)
			{
				const uint32 sum1 = core::RotateRight(e, 6) ^ core::RotateRight(e, 11) ^ core::RotateRight(e, 25);
				const uint32 choice = (e & f) ^ (~e & g);
				const uint32 temp1 = h + sum1 + choice + ROUND_CONSTANTS[index] + words[index];
				const uint32 sum0 = core::RotateRight(a, 2) ^ core::RotateRight(a, 13) ^ core::RotateRight(a, 22);
				const uint32 majority = (a & b) ^ (a & c) ^ (b & c);
				const uint32 temp2 = sum0 + majority;

				h = g;
				g = f;
				f = e;
				e = d + temp1;
				d = c;
				c = b;
				b = a;
				a = temp1 + temp2;
			}

			state[0] += a;
			state[1] += b;
			state[2] += c;
			state[3] += d;
			state[4] += e;
			state[5] += f;
			state[6] += g;
			state[7] += h;
		}

	} // namespace

	void ProcessScalarBlocks(const uint8* blocks, usize count, StaticArray<uint32, 8>& state) noexcept
	{
		ASSERT(blocks != nullptr || count == 0, "Nonempty SHA-256 blocks require readable storage.");

		for (usize index = 0; index < count; ++index)
		{
			processBlock(blocks + index * 64, state);
		}
	}

	void ProcessBlocks(const uint8* blocks, usize count, StaticArray<uint32, 8>& state) noexcept
	{
		static const auto process = IsHardwareSupported() ? &ProcessHardwareBlocks : &ProcessScalarBlocks;
		process(blocks, count, state);
	}
} // namespace rox::core
