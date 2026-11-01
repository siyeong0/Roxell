#pragma once

#include <Result.hpp>
#include <Span.hpp>
#include <Types.h>

namespace rox::os
{
	// Fills caller-owned bytes from the operating system cryptographic random source.
	[[nodiscard]] core::Result<void> FillWindowsRandom(core::Span<core::Byte> bytes);
}
