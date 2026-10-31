#include "Sha256_blocks.h"
#include <Vocabulary.hpp>
#include <Types.h>
#include <Utility.hpp>
#include <Assertion.h>
#include <bit>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace rox::core
{
	namespace
	{
		bool detectHardware() noexcept
		{
#if defined(_M_X64) || defined(__x86_64__)
#if defined(_MSC_VER)
			int registers[4]{};
			__cpuidex(registers, 0, 0);

			if (registers[0] < 7)
			{
				return false;
			}

			__cpuidex(registers, 1, 0);

			if ((registers[2] & (1 << 9)) == 0 || (registers[3] & (1 << 26)) == 0)
			{
				return false;
			}

			__cpuidex(registers, 7, 0);
			return (registers[1] & (1 << 29)) != 0;
#else
			unsigned int a = 0;
			unsigned int b = 0;
			unsigned int c = 0;
			unsigned int d = 0;

			if (__get_cpuid_max(0, nullptr) < 7 || !__get_cpuid(1, &a, &b, &c, &d) || (c & (1u << 9)) == 0 ||
				(d & (1u << 26)) == 0)
			{
				return false;
			}

			__cpuid_count(7, 0, a, b, c, d);
			return (b & (1u << 29)) != 0;
#endif
#else
			return false;
#endif
		}

#if defined(_M_X64) || defined(__x86_64__)
#if defined(__GNUC__) || defined(__clang__)
		__attribute__((target("sha,ssse3"), noinline))
#else
		__declspec(noinline)
#endif
		void compressHardware(const uint8* blocks, rox::core::usize count, rox::core::StaticArray<uint32, 8>& state) noexcept
		{
			// SHA256RNDS2 carries A/B/E/F and C/D/G/H in descending lanes; each instruction advances two rounds.
			// Instruction semantics: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sha-extensions.html
			__m128i abef = _mm_set_epi32(rox::core::BitCast<int>(state[0]), rox::core::BitCast<int>(state[1]),
										 rox::core::BitCast<int>(state[4]), rox::core::BitCast<int>(state[5]));
			__m128i cdgh = _mm_set_epi32(rox::core::BitCast<int>(state[2]), rox::core::BitCast<int>(state[3]),
										 rox::core::BitCast<int>(state[6]), rox::core::BitCast<int>(state[7]));
			const __m128i byteOrder = _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);

			for (rox::core::usize block = 0; block < count; ++block)
			{
				const __m128i savedAbef = abef;
				const __m128i savedCdgh = cdgh;
				__m128i schedule[4];

				for (uint32 group = 0; group < 16; ++group)
				{
					const uint32 slot = group & 3;

					if (group < 4)
					{
						schedule[slot] = _mm_shuffle_epi8(
							_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + block * 64 + group * 16)),
							byteOrder);
					}
					else
					{
						const __m128i previous = schedule[(group - 1) & 3];
						const __m128i minusSeven = _mm_alignr_epi8(previous, schedule[(group - 2) & 3], 4);
						const __m128i first = _mm_sha256msg1_epu32(schedule[slot], schedule[(group - 3) & 3]);
						schedule[slot] = _mm_sha256msg2_epu32(_mm_add_epi32(first, minusSeven), previous);
					}

					const __m128i constants =
						_mm_loadu_si128(reinterpret_cast<const __m128i*>(ROUND_CONSTANTS.data() + group * 4));
					const __m128i message = _mm_add_epi32(schedule[slot], constants);
					cdgh = _mm_sha256rnds2_epu32(cdgh, abef, message);
					abef = _mm_sha256rnds2_epu32(abef, cdgh, _mm_shuffle_epi32(message, 0x0e));
				}

				abef = _mm_add_epi32(abef, savedAbef);
				cdgh = _mm_add_epi32(cdgh, savedCdgh);
			}

			uint32 lanes[8];
			_mm_storeu_si128(reinterpret_cast<__m128i*>(lanes), abef);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(lanes + 4), cdgh);
			state = {lanes[3], lanes[2], lanes[7], lanes[6], lanes[1], lanes[0], lanes[5], lanes[4]};
		}
#endif
	} // namespace

	bool IsHardwareSupported() noexcept
	{
		static const bool bSupported = detectHardware();
		return bSupported;
	}

	void ProcessHardwareBlocks(const uint8* blocks, rox::core::usize count, rox::core::StaticArray<uint32, 8>& state) noexcept
	{
		ASSERT(IsHardwareSupported(), "SHA-256 instructions require CPU feature admission.");
		ASSERT(blocks != nullptr || count == 0, "Nonempty SHA-256 blocks require readable storage.");
#if defined(_M_X64) || defined(__x86_64__)
		compressHardware(blocks, count, state);
#else
		(void)blocks;
		(void)count;
		(void)state;
#endif
	}
} // namespace rox::core
