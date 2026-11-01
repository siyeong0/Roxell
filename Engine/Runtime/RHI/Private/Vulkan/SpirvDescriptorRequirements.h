#pragma once

#include <RhiDeviceTypes.h>
#include <Result.hpp>
#include <Span.hpp>

namespace rox::rhi
{
	// Checks native indexing and fragment-demote capabilities independently of serialized shader metadata.
	[[nodiscard]] core::Result<void> ValidateSpirvDescriptorIndexing(core::Span<const core::Byte> bytecode, const GpuCapabilities& capabilities) noexcept;
}
