#pragma once

#include <Types.h>

namespace rox::core
{
	// Logging and tracing share this thread ID within one linked binary image.
	uint64 diagnosticThreadId() noexcept;
}
