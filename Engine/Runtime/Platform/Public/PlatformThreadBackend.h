#pragma once

#include <IThreadBackend.h>

namespace rox::os
{
	// The process-lifetime backend borrows each entry until Join completes.
	[[nodiscard]] core::IThreadBackend& GetPlatformThreadBackend() noexcept;
} // namespace rox::os
