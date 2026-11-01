#include <WindowsRandom.h>
#include <NumericLimits.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>

namespace rox::os
{
	core::Result<void> FillWindowsRandom(core::Span<core::Byte> bytes)
	{
		if (bytes.Size() > rox::core::NumericLimits<ULONG>::max())
		{
			return core::Result<void>::Failure(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-RANDOM-BOUNDS", "The random byte request exceeds the native size bound.");
		}

		if (bytes.IsEmpty())
		{
			return core::Result<void>::Success();
		}

		if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.Data()), static_cast<ULONG>(bytes.Size()),
			BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
		{
			return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-RANDOM", "The operating system random source failed.");
		}

		return core::Result<void>::Success();
	}
}
