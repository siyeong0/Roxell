#pragma once

#include <ShaderBuilder.h>

namespace rox::shader
{
	class Reader;
	class Writer;

	bool HasDescription(const ShaderBuilder&) noexcept;
	core::Result<void> ValidateDescription(const ShaderBuilder&, const ShaderLimits&);

	core::Result<void> WriteDescription(Writer&, const ShaderBuilder&);
	core::Result<void> ReadDescription(Reader&, ShaderBuilder&, const ShaderLimits&);

	core::Result<void> CloneDescription(const ShaderBuilder&, ShaderBuilder&, const ShaderLimits&);

	core::Result<core::Array<core::Byte>> WrapShaderDescription(core::Array<core::Byte>, const ShaderBuilder&, core::MemoryResource&);
	core::Result<ShaderBuilder> LoadShaderDescription(core::Span<const core::Byte>, core::MemoryResource&, const ShaderLimits&, core::CancellationToken);
} // namespace rox::shader
