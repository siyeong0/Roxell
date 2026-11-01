#pragma once

#include "../ShaderCompilerInternal.h"

namespace rox::shader
{
	[[nodiscard]] core::Result<void> reflectDxcSpirv(
		shader::ShaderVariant& variant,
		shader::ShaderEntryPoint& entry,
		core::MemoryResource& memory,
		core::CancellationToken cancellation = {});
} // namespace rox::shader
