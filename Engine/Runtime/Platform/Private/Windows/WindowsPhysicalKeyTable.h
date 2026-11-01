#pragma once

#include <WindowTypes.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace rox::os
{
	// Native scan codes take precedence; virtual-only input uses the current thread's keyboard layout.
	[[nodiscard]] EPhysicalKey PhysicalKeyFromScanCode(WPARAM virtualKey, LPARAM keyData) noexcept;
} // namespace rox::os
