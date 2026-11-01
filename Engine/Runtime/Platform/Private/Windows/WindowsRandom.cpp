#include <WindowsRandom.h>
#include <NumericLimits.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>

namespace rox::os
{
	using namespace core;

	Result<void> FillWindowsRandom(Span<Byte> bytes)
	{
		if (bytes.Size() > NumericLimits<ULONG>::max())
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "ROX-PLATFORM-RANDOM-BOUNDS", "The random byte request exceeds the native size bound.");
		}

		if (bytes.IsEmpty())
		{
			return Result<void>::Success();
		}

		if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.Data()), static_cast<ULONG>(bytes.Size()),
			BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "ROX-PLATFORM-RANDOM", "The operating system random source failed.");
		}

		return Result<void>::Success();
	}
}
