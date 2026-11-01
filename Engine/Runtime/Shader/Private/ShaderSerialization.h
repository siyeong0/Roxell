#pragma once

#include <ShaderBuilder.h>

namespace rox::shader
{
	class Writer;

	[[nodiscard]] core::Result<core::Array<core::Byte>> SaveShader(const ShaderBuilder&, EShaderFormat, core::MemoryResource&, core::CancellationToken = {});
	[[nodiscard]] core::Result<core::Array<core::Byte>> SaveShader(const ShaderBuilder&, core::MemoryResource&, core::CancellationToken = {});
	[[nodiscard]] core::Result<core::Hash256> ComputeShaderIdentity(const ShaderBuilder&, core::MemoryResource&);

	core::Result<ShaderBuilder> DecodeShaderArtifactDraft(core::Span<const core::Byte>, core::MemoryResource&, const ShaderLimits&, core::CancellationToken);
	core::Result<void> WriteCanonicalArtifact(Writer&, const ShaderBuilder&, core::CancellationToken);

	core::Result<ShaderBuilder> CanonicalizeValidatedDraft(ShaderBuilder, core::MemoryResource&, core::CancellationToken);
} // namespace rox::shader
