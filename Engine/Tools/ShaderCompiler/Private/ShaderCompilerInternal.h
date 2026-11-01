#pragma once

#include "SourceClosure.h"

#include <Algorithms.hpp>
#include <ByteWriter.h>
#include <Cancellation.h>
#include <ShaderBuilder.h>
#include <ShaderCompiler.h>
#include <TextPrimitives.hpp>
#include <Utf8.h>

namespace rox::shader
{
	core::Error MakeBuildError(core::EErrorCode code, core::StringView message);
	core::Error MakeBuildAllocationError();
	core::Error MakeBuildCancelledError();
	core::Hash256 HashText(core::StringView text);
	void RemoveUnusedBinding(shader::ShaderVariant& variant, core::uint32 index);
	[[nodiscard]] bool IsCanonicalIdentity(core::StringView);
	[[nodiscard]] core::Result<core::String> CanonicalPath(core::StringView base, core::StringView path, core::MemoryResource&);
	[[nodiscard]] core::Result<void> ValidateRequest(const ShaderCompileRequest&, EShaderCompiler);
	void AddDiagnostics(ShaderBuildReport&, core::StringView, const ShaderCompileRequest&, core::uint32 entry = 0xffffffffu, core::uint32 target = 0xffffffffu);
	[[nodiscard]] core::Result<void> ValidateProfiles(const ShaderCompileRequest&, core::Span<const ShaderCompilerProfile>, core::uint32 abi);
	[[nodiscard]] core::Result<void> ValidatePlan(const ShaderBuildPlan&, const IShaderCompiler&);
	[[nodiscard]] core::Result<void> PrepareBuildIdentity(
		ShaderBuildOutput&,
		const ShaderCompileRequest&,
		const SourceClosure&,
		const ShaderToolchainIdentity&,
		core::MemoryResource&,
		core::CancellationToken);
	[[nodiscard]] core::Result<void> FinishBuild(shader::ShaderBuilder&, ShaderBuildOutput&, const ShaderBuildPlan&, core::MemoryResource&, core::CancellationToken);

	constexpr ShaderCompilerProfile CompilerProfile(
		EShaderSourceLanguage language,
		shader::EShaderTarget target,
		core::uint32 abi = shader::SHADER_ABI_VERSION,
		bool bNativeSpecialization = false,
		bool bModuleLink = false,
		bool bGenericEntrySpecialization = false) noexcept
	{
		ShaderCompilerProfile profile;
		profile.Language = language;
		profile.ShaderAbiVersion = abi;
		profile.ProfileVersion = 1;
		profile.bModuleCompilation = bModuleLink;
		profile.bLinking = bModuleLink;
		profile.bSpecialization = bNativeSpecialization;
		profile.bGenericEntrySpecialization = bGenericEntrySpecialization;
		profile.Target.Target = target;
		profile.Profile = target == shader::EShaderTarget::Dxil ? EShaderCompilerProfile::DxilRasterCompute66 : EShaderCompilerProfile::SpirvRasterCompute16;

		return profile;
	}

	constexpr ShaderCompilerProfile MeshCompilerProfile(shader::EShaderTarget target, core::uint32 abi = shader::SHADER_ABI_VERSION) noexcept
	{
		ShaderCompilerProfile profile = CompilerProfile(EShaderSourceLanguage::Slang, target, abi);
		profile.ProfileVersion = 1;
		profile.Profile = target == shader::EShaderTarget::Dxil ? EShaderCompilerProfile::DxilMeshRaster66 : EShaderCompilerProfile::SpirvMeshRaster16;
		profile.StageMask = (core::uint64(1) << static_cast<core::uint32>(shader::EShaderStage::Mesh)) |
			(core::uint64(1) << static_cast<core::uint32>(shader::EShaderStage::Pixel));
		profile.StageMask |= core::uint64(1) << static_cast<core::uint32>(shader::EShaderStage::Task);

		return profile;
	}

	constexpr ShaderCompilerProfile RayCompilerProfile(shader::EShaderTarget target) noexcept
	{
		ShaderCompilerProfile profile = CompilerProfile(EShaderSourceLanguage::Slang, target, shader::SHADER_ABI_VERSION);
		profile.ProfileVersion = 1;
		profile.Profile = target == shader::EShaderTarget::Dxil ? EShaderCompilerProfile::DxilRay66 : EShaderCompilerProfile::SpirvRay16;
		profile.StageMask = 0;

		for (core::uint32 stage = static_cast<core::uint32>(shader::EShaderStage::RayGeneration); stage <= static_cast<core::uint32>(shader::EShaderStage::Callable); ++stage)
		{
			profile.StageMask |= core::uint64{1} << stage;
		}

		return profile;
	}

	[[nodiscard]] core::Result<core::InterfaceOwner<IShaderCompiler>> CreateSlang(const ShaderCompilerOptions&, core::MemoryResource&);
	[[nodiscard]] core::Result<core::InterfaceOwner<IShaderCompiler>> CreateDxc(const ShaderCompilerOptions&, core::MemoryResource&);
	[[nodiscard]] core::Result<core::uint64> ReflectDxilCapabilities(core::Span<const core::Byte>, core::MemoryResource&, core::uint32 abi = shader::SHADER_ABI_VERSION);
} // namespace rox::shader
