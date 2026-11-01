#pragma once

#include <ShaderLayout.h>

namespace rox::shader
{
	class Reader;
	class Writer;

	core::Result<void> ValidateLocalRootMetadata(const ShaderVariant&, EShaderStage, const ShaderLimits&, core::uint64& totalBytes);

	core::Result<void> WriteLocalRootMetadata(Writer&, const ShaderVariant&, const core::Array<core::uint32>* bindingMap);
	core::Result<void> ReadLocalRootMetadata(Reader&, ShaderVariant&, const ShaderLimits&);
} // namespace rox::shader
