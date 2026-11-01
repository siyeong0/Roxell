#include <ShaderCompiler.h>

#include "ShaderCompilerInternal.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <Cancellation.h>
#include <Sha256.h>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	Result<InterfaceOwner<IShaderCompiler>> CreateShaderCompiler(EShaderCompiler compiler, MemoryResource& resource)
	{
		return CreateShaderCompiler(compiler, ShaderCompilerOptions{}, resource);
	}

	Result<InterfaceOwner<IShaderCompiler>> CreateShaderCompiler(EShaderCompiler compiler, const ShaderCompilerOptions& options, MemoryResource& resource)
	{
		if (options.ShaderAbiVersion != shader::SHADER_ABI_VERSION)
		{
			return Result<InterfaceOwner<IShaderCompiler>>::Failure(MakeBuildError(EErrorCode::Unsupported, "Unknown shader compiler output ABI."));
		}

		switch (compiler)
		{
		case EShaderCompiler::Slang:
			return CreateSlang(options, resource);
		case EShaderCompiler::Dxc:
			return CreateDxc(options, resource);
		default:
			return Result<InterfaceOwner<IShaderCompiler>>::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Unknown shader compiler selection."));
		}
	}

	Result<ShaderBuildPlan> IShaderCompiler::PrepareLink(
		const ShaderLinkRequest& request,
		Span<const ShaderModuleArtifact> modules,
		ShaderBuildReport& report,
		MemoryResource& resource,
		CancellationToken cancellation)
	{
		report.Diagnostics.Clear();
		report.bTruncated = false;
		report.bAllocationFailure = false;

		return ShaderBuildPlan::CaptureLink(*this, request, modules, resource, cancellation);
	}

	Result<ShaderModuleArtifact> IShaderCompiler::CompileModule(const ShaderBuildPlan&, ShaderModuleSelection, ShaderBuildReport& report, MemoryResource&, CancellationToken)
	{
		report.Diagnostics.Clear();
		report.bTruncated = false;
		report.bAllocationFailure = false;

		return Result<ShaderModuleArtifact>::Failure(MakeBuildError(EErrorCode::Unsupported, "This compiler does not implement module compilation."));
	}

	Result<ShaderBuildOutput> IShaderCompiler::LinkProgram(const ShaderBuildPlan&, ShaderBuildReport& report, MemoryResource&, CancellationToken)
	{
		report.Diagnostics.Clear();
		report.bTruncated = false;
		report.bAllocationFailure = false;

		return Result<ShaderBuildOutput>::Failure(MakeBuildError(EErrorCode::Unsupported, "This compiler does not implement module linking."));
	}

	Result<ShaderBuildPlan> IShaderCompiler::PrepareBuild(
		const ShaderCompileRequest& request,
		IShaderSourceResolver& resolver,
		ShaderBuildReport& report,
		MemoryResource& resource,
		CancellationToken cancellation)
	{
		report.Diagnostics.Clear();
		report.bTruncated = false;
		report.bAllocationFailure = false;

		return ShaderBuildPlan::Capture(*this, request, resolver, resource, cancellation);
	}

	Result<ShaderBuildOutput> IShaderCompiler::Compile(
		const ShaderCompileRequest& request,
		IShaderSourceResolver& resolver,
		ShaderBuildReport& report,
		MemoryResource& resource,
		CancellationToken cancellation)
	{
		Result<ShaderBuildPlan> prepared = PrepareBuild(request, resolver, report, resource, cancellation);
		if (!prepared)
		{
			return Result<ShaderBuildOutput>::Failure(Move(prepared.GetError()));
		}

		return Compile(prepared.Value(), report, resource, cancellation);
	}
} // namespace rox::shader
