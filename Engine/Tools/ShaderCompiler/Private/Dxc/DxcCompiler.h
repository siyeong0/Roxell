#pragma once

#include "../Dxil/DxcPointer.hpp"
#include "../ShaderCompilerInternal.h"

#include <Cancellation.h>

#if defined(_WIN32)
namespace rox::shader
{
	class DxcCompiler final : public IShaderCompiler
	{
	public:
		explicit DxcCompiler(const ShaderCompilerOptions& options, core::MemoryResource& resource) noexcept;
		core::Result<void> Initialize();
		ShaderCompilerCapabilities GetCapabilities() const noexcept override;
		const ShaderToolchainIdentity& GetToolchainIdentity() const noexcept override;
		core::Span<const ShaderCompilerProfile> GetProfiles() const noexcept override;
		core::Result<ShaderBuildOutput> Compile(const ShaderBuildPlan& plan, ShaderBuildReport& report, core::MemoryResource& resource, core::CancellationToken cancellation)
			override;

	private:
		DxcPointer<IDxcUtils> m_Utils;
		DxcPointer<IDxcCompiler3> m_Compiler;
		ShaderToolchainIdentity m_Toolchain;
	};

} // namespace rox::shader
#endif
