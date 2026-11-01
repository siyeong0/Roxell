#pragma once

#include "DxcSdk.h"
#include "../ShaderCompilerInternal.h"

namespace rox::shader
{
	core::Result<void> ReflectDxilMetadata(
		ShaderVariant& variant,
		const ShaderEntryPoint& entry,
		core::MemoryResource& resource,
		core::CancellationToken cancellation,
		core::uint32 abi = SHADER_ABI_VERSION);
	core::Result<void> ReflectDxilBindings(ShaderVariant& variant, const ShaderEntryPoint& entry, core::CancellationToken cancellation);
	core::Result<void> ReflectDxilRayMetadata(ShaderVariant& variant, const ShaderEntryPoint& entry, core::CancellationToken cancellation, core::uint32 abi = SHADER_ABI_VERSION);
	core::Result<void> ReflectDxilLocalRoot(
		ShaderVariant& variant,
		const ShaderEntryPoint& entry,
		const ShaderCompileRequest& request,
		core::MemoryResource& resource,
		core::CancellationToken cancellation);
#if defined(_WIN32)
	bool MatchesDxilBinding(const ShaderBinding& binding, const D3D12_SHADER_INPUT_BIND_DESC& native);
#endif
} // namespace rox::shader
