#pragma once

#include <Cancellation.h>
#include <ShaderCompiler.h>

#include <slang-com-ptr.h>

namespace rox::shader
{
	class SlangCompiler final : public IShaderCompiler
	{
	public:
		explicit SlangCompiler(const ShaderCompilerOptions& options, core::MemoryResource& resource) noexcept;
		core::Result<void> Initialize(core::MemoryResource& resource);
		ShaderCompilerCapabilities GetCapabilities() const noexcept override;
		const ShaderToolchainIdentity& GetToolchainIdentity() const noexcept override;
		core::Span<const ShaderCompilerProfile> GetProfiles() const noexcept override;
		core::Result<ShaderBuildOutput> Compile(const ShaderBuildPlan& plan, ShaderBuildReport& report, core::MemoryResource& resource, core::CancellationToken cancellation) override;
		core::Result<ShaderModuleArtifact> CompileModule(const ShaderBuildPlan&, ShaderModuleSelection, ShaderBuildReport&, core::MemoryResource&, core::CancellationToken)
			override;
		core::Result<ShaderBuildOutput> LinkProgram(const ShaderBuildPlan&, ShaderBuildReport&, core::MemoryResource&, core::CancellationToken) override;

	private:
		core::Result<Slang::ComPtr<slang::ISession>> createSession(const ShaderCompileRequest&, ISlangFileSystem*, core::MemoryResource&);
		core::Result<ShaderBuildOutput> buildProgram(const ShaderBuildPlan&, ShaderBuildReport&, core::MemoryResource&, core::CancellationToken);
		core::Result<void> captureSpecializationParameters(slang::IEntryPoint&, ShaderModuleEntryRecord&, core::MemoryResource&);
		core::Result<Slang::ComPtr<slang::IComponentType>> specializeEntry(
			const ShaderLinkEntrySelection&,
			core::Span<slang::IModule* const>,
			slang::IComponentType&,
			ShaderBuildReport&,
			const ShaderCompileRequest&,
			core::MemoryResource&);

	private:
		Slang::ComPtr<slang::IGlobalSession> m_GlobalSession;
		ShaderToolchainIdentity m_Toolchain;
	};

} // namespace rox::shader
