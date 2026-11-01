#pragma once

#include <ShaderLayout.h>

namespace rox::shader
{
	class Reader;
	class Writer;

	bool IsRayStage(EShaderStage) noexcept;

	bool IsDefaultRayInterface(const ShaderRayInterface&) noexcept;

	core::Result<void> ValidateRayInterface(const ShaderRayInterface&, const ShaderEntryPoint&, EShaderTarget, const ShaderLimits&, core::uint64& totalBytes);

	core::Result<void> CanonicalizeRayInterface(ShaderRayInterface&);
	bool IsCanonicalRayInterface(const ShaderRayInterface&) noexcept;

	core::Result<void> CloneRayInterface(const ShaderRayInterface&, ShaderRayInterface&);

	core::Result<void> WriteRayInterface(Writer&, const ShaderRayInterface&, core::MemoryResource&, bool bCanonical);
	core::Result<void> ReadRayInterface(Reader&, ShaderRayInterface&, const ShaderLimits&, core::uint64& totalSignatures, core::uint64& totalTypes, core::uint64& totalMembers);
} // namespace rox::shader
