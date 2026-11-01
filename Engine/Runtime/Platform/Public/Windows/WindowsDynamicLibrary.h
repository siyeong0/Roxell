#pragma once

#include <DynamicLibraryOwner.h>

namespace rox::os
{
	// Only the explicit absolute UTF-8 image path, its directory and System32 participate in loading.
	[[nodiscard]] core::Result<DynamicLibraryOwner> LoadWindowsDynamicLibrary(core::MemoryResource& resource, core::StringView absolutePathUtf8) noexcept;
} // namespace rox::os
