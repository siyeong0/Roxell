#pragma once

#include <Hash.h>
#include <Result.hpp>

namespace rox::os
{
	struct WindowsExecutionEnvironment final
	{
		core::Hash256 Fingerprint;

		// Nearest rounding, gradual underflow and all SSE exception masks set.
		bool bDefaultFloatingPointMode = false;
	};

	// Current x64 CPU vendor/signature/features, enabled XSTATE and calling-thread FP controls.
	// Excludes APIC IDs, processor topology, FP sticky status flags and addresses.
	// Does not alter FP state or allocate on success. Unsupported architectures fail explicitly.
	[[nodiscard]] core::Result<WindowsExecutionEnvironment> ReadWindowsExecutionEnvironment();
}
