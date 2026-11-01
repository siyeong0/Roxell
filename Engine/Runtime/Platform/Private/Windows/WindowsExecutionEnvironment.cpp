#include <WindowsExecutionEnvironment.h>
#include <FloatEnvironment.hpp>
#include <Types.h>
#include <Assertion.h>

#if defined(_M_X64)
#include <intrin.h>
#include <xmmintrin.h>
#endif

namespace rox::os
{
	core::Result<WindowsExecutionEnvironment> ReadWindowsExecutionEnvironment()
	{
#if defined(_M_X64)
		constexpr char HASH_DOMAIN[] = "Roxell.Platform.Windows.ExecutionEnvironment.0.1";
		core::Byte bytes[sizeof(HASH_DOMAIN) + 22 * 4];
		rox::core::usize offset = 0;

		for (const char value : HASH_DOMAIN)
		{
			bytes[offset++] = static_cast<core::Byte>(value);
		}

		const auto word = [&](core::uint32 value)
			{
				for (core::uint32 shift = 0; shift < 32; shift += 8)
				{
					bytes[offset++] = static_cast<core::Byte>((value >> shift) & 0xffu);
				}
			};
		int registers[4]{};
		__cpuidex(registers, 0, 0);
		const core::uint32 maximumLeaf = static_cast<core::uint32>(registers[0]);

		for (const int value : registers)
		{
			word(static_cast<core::uint32>(value));
		}

		int feature[4]{};

		if (maximumLeaf >= 1)
		{
			__cpuidex(feature, 1, 0);
		}
		// EBX contains logical-processor/APIC identifiers and is intentionally omitted.
		word(static_cast<core::uint32>(feature[0]));
		word(static_cast<core::uint32>(feature[2]));
		word(static_cast<core::uint32>(feature[3]));
		int extended[4]{};

		if (maximumLeaf >= 7)
		{
			__cpuidex(extended, 7, 0);
		}

		for (const int value : extended)
		{
			word(static_cast<core::uint32>(value));
		}

		int subleaf[4]{};

		if (maximumLeaf >= 7 && extended[0] >= 1)
		{
			__cpuidex(subleaf, 7, 1);
		}

		for (const int value : subleaf)
		{
			word(static_cast<core::uint32>(value));
		}

		__cpuidex(registers, static_cast<int>(0x80000000u), 0);
		const core::uint32 maximumExtendedLeaf = static_cast<core::uint32>(registers[0]);
		word(maximumExtendedLeaf);
		int extendedFeature[4]{};

		if (maximumExtendedLeaf >= 0x80000001u)
		{
			__cpuidex(extendedFeature, static_cast<int>(0x80000001u), 0);
		}

		word(static_cast<core::uint32>(extendedFeature[2]));
		word(static_cast<core::uint32>(extendedFeature[3]));
		constexpr core::uint32 XSAVE_AND_OSXSAVE = (1u << 26) | (1u << 27);
		const core::uint64 xstate = (static_cast<core::uint32>(feature[2]) & XSAVE_AND_OSXSAVE) == XSAVE_AND_OSXSAVE ? static_cast<core::uint64>(_xgetbv(0)) : 0;
		word(static_cast<core::uint32>(xstate));
		word(static_cast<core::uint32>(xstate >> 32));
		const core::uint32 floatingControl = static_cast<core::uint32>(_mm_getcsr()) & 0x0000ffc0u;
		const int rounding = rox::core::GetRoundingMode();
		word(floatingControl);
		word(static_cast<core::uint32>(rounding));
		ASSERT(offset == sizeof(bytes), "Execution environment material must have its declared byte width.");

		return core::Result<WindowsExecutionEnvironment>::Success({ core::ComputeSha256(bytes), floatingControl == 0x00001f80u && rounding == FE_TONEAREST });
#else
		return core::Result<WindowsExecutionEnvironment>::Failure(core::EErrorCode::Unsupported, "ROX-PLATFORM-EXECUTION-ENVIRONMENT",
			"Execution environment fingerprinting requires Windows x64.",
			"Use a supported execution environment.");
#endif
	}
}
