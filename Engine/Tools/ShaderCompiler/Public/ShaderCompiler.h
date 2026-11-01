#pragma once

#include <Cancellation.h>
#include <ShaderCompilerTypes.h>
#include <ShaderBuildPlan.h>

namespace rox::shader
{
	class IShaderSourceResolver
	{
	public:
		virtual ~IShaderSourceResolver() = default;

		[[nodiscard]] virtual core::Result<ShaderSourceSnapshot> Resolve(
			core::StringView requestingSource,
			core::StringView requestedPath,
			core::MemoryResource& resource,
			core::CancellationToken cancellation) = 0;
	};

	class IShaderCompiler
	{
	public:
		virtual ~IShaderCompiler() = default;

		[[nodiscard]] virtual ShaderCompilerCapabilities GetCapabilities() const noexcept = 0;
		[[nodiscard]] virtual const ShaderToolchainIdentity& GetToolchainIdentity() const noexcept = 0;
		[[nodiscard]] virtual core::Span<const ShaderCompilerProfile> GetProfiles() const noexcept = 0;

		[[nodiscard]] core::Result<ShaderBuildPlan> PrepareBuild(
			const ShaderCompileRequest& request,
			IShaderSourceResolver& resolver,
			ShaderBuildReport& report,
			core::MemoryResource& resource,
			core::CancellationToken cancellation = {});
		[[nodiscard]] core::Result<ShaderBuildPlan> PrepareLink(
			const ShaderLinkRequest& request,
			core::Span<const ShaderModuleArtifact> modules,
			ShaderBuildReport& report,
			core::MemoryResource& resource,
			core::CancellationToken cancellation = {});

		// Prepared compilation requires a valid plan; source and toolchain failures return Result errors.
		[[nodiscard]] virtual core::Result<ShaderModuleArtifact> CompileModule(
			const ShaderBuildPlan& plan,
			ShaderModuleSelection selection,
			ShaderBuildReport& report,
			core::MemoryResource& resource,
			core::CancellationToken cancellation = {});
		[[nodiscard]] virtual core::Result<ShaderBuildOutput> LinkProgram(
			const ShaderBuildPlan& plan,
			ShaderBuildReport& report,
			core::MemoryResource& resource,
			core::CancellationToken cancellation = {});
		[[nodiscard]] virtual core::Result<ShaderBuildOutput> Compile(
			const ShaderBuildPlan& plan,
			ShaderBuildReport& report,
			core::MemoryResource& resource,
			core::CancellationToken cancellation = {}) = 0;
		[[nodiscard]] core::Result<ShaderBuildOutput> Compile(
			const ShaderCompileRequest& request,
			IShaderSourceResolver& resolver,
			ShaderBuildReport& report,
			core::MemoryResource& resource,
			core::CancellationToken cancellation = {});
	};

	[[nodiscard]] core::Result<ShaderSourceSnapshot> CreateShaderSourceSnapshot(core::StringView identity, core::StringView source, core::MemoryResource& resource);
	[[nodiscard]] core::Result<core::InterfaceOwner<IShaderCompiler>> CreateShaderCompiler(
		EShaderCompiler compiler,
		const ShaderCompilerOptions& options,
		core::MemoryResource& resource);
	[[nodiscard]] core::Result<core::InterfaceOwner<IShaderCompiler>> CreateShaderCompiler(EShaderCompiler compiler, core::MemoryResource& resource);
} // namespace rox::shader
