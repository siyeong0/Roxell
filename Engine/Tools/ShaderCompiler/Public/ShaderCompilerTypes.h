#pragma once

#include <InterfaceOwner.hpp>
#include <Shader.h>

namespace rox::shader
{
	enum class EShaderCompiler : core::uint32
	{
		Slang,
		Dxc
	};

	enum class EShaderSourceLanguage : core::uint32
	{
		Slang,
		Hlsl
	};

	enum class EShaderSourceBuildPurpose : core::uint32
	{
		Program,
		Module
	};

	enum class EShaderBuildPlanKind : core::uint32
	{
		SourceProgram,
		SourceModule,
		LinkedProgram
	};

	enum class EShaderOptimization : core::uint32
	{
		None,
		Default,
		High,
		Maximum
	};

	enum class EShaderDebugInfo : core::uint32
	{
		None,
		Standard
	};

	enum class EShaderDiagnosticSeverity : core::uint32
	{
		Note,
		Warning,
		Error
	};

	struct ShaderSourceSnapshot
	{
		core::String Identity;
		core::String Source;
		core::Hash256 ContentHash;

		explicit ShaderSourceSnapshot(core::MemoryResource& resource) noexcept
			: Identity(resource)
			, Source(resource)
		{
		}

		ShaderSourceSnapshot(ShaderSourceSnapshot&&) noexcept = default;
		ShaderSourceSnapshot& operator=(ShaderSourceSnapshot&&) noexcept = default;
		ShaderSourceSnapshot(const ShaderSourceSnapshot&) = delete;
		ShaderSourceSnapshot& operator=(const ShaderSourceSnapshot&) = delete;
	};

	struct ShaderDefine
	{
		core::String Name;
		core::String Value;

		explicit ShaderDefine(core::MemoryResource& resource) noexcept
			: Name(resource)
			, Value(resource)
		{
		}

		ShaderDefine(ShaderDefine&&) noexcept = default;
		ShaderDefine& operator=(ShaderDefine&&) noexcept = default;
		ShaderDefine(const ShaderDefine&) = delete;
		ShaderDefine& operator=(const ShaderDefine&) = delete;
	};

	struct ShaderBuildLimits
	{
		core::uint32 MaximumSources = 1024;
		core::uint32 MaximumSourceBytes = 4 * 1024 * 1024;
		core::uint64 MaximumAggregateSourceBytes = 32 * 1024 * 1024;
		core::uint32 MaximumTraversalDepth = 64;

		core::uint32 MaximumDiagnostics = 4096;
		core::uint32 MaximumDiagnosticBytes = 1024 * 1024;

		core::uint32 MaximumModules = 1024;
		core::uint32 MaximumModuleBytes = 16 * 1024 * 1024;
		core::uint64 MaximumAggregateModuleBytes = 64 * 1024 * 1024;
	};

	struct ShaderLocalRootArgumentDeclaration
	{
		shader::EShaderLocalRootArgumentKind Kind = shader::EShaderLocalRootArgumentKind::Constants32;
		core::String BindingName;
		core::uint32 ValueCount = 0;

		explicit ShaderLocalRootArgumentDeclaration(core::MemoryResource& resource) noexcept
			: BindingName(resource)
		{
		}
	};

	struct ShaderLocalRootRecipe
	{
		core::String EntryPointName;
		core::Array<ShaderLocalRootArgumentDeclaration> Arguments;

		explicit ShaderLocalRootRecipe(core::MemoryResource& resource) noexcept
			: EntryPointName(resource)
			, Arguments(resource)
		{
		}
	};

	struct ShaderCompileRequest
	{
		EShaderSourceBuildPurpose Purpose = EShaderSourceBuildPurpose::Program;
		EShaderSourceLanguage Language = EShaderSourceLanguage::Slang;

		ShaderSourceSnapshot Root;
		core::Array<shader::ShaderEntryPoint> EntryPoints;
		core::Array<shader::ShaderTargetProfile> Targets;
		core::Array<ShaderDefine> Defines;
		core::Array<core::String> IncludeRoots;

		EShaderOptimization Optimization = EShaderOptimization::Default;
		EShaderDebugInfo DebugInfo = EShaderDebugInfo::None;
		shader::EShaderMatrixOrder MatrixOrder = shader::EShaderMatrixOrder::RowMajor;
		bool bWarningsAsErrors = false;
		bool bPreciseFloatingPoint = false;

		core::uint32 LanguageVersion = 2021;
		core::uint32 EngineProfileVersion = 1;

		ShaderBuildLimits Limits;

		shader::ShaderRayDeclaredLimits RayDeclaredLimits;
		core::Array<ShaderLocalRootRecipe> LocalRootRecipes;

		explicit ShaderCompileRequest(core::MemoryResource& resource) noexcept
			: Root(resource)
			, EntryPoints(resource)
			, Targets(resource)
			, Defines(resource)
			, IncludeRoots(resource)
			, LocalRootRecipes(resource)
		{
		}
	};

	struct ShaderDiagnostic
	{
		EShaderDiagnosticSeverity Severity = EShaderDiagnosticSeverity::Error;
		core::String DiagnosticID;
		core::String CompilerCode;
		core::String SourceIdentity;
		core::String Message;

		core::uint32 Line = 0;
		core::uint32 Column = 0;

		core::uint32 EntryPointIndex = 0xffffffffu;
		core::uint32 TargetIndex = 0xffffffffu;

		explicit ShaderDiagnostic(core::MemoryResource& resource) noexcept
			: DiagnosticID(resource)
			, CompilerCode(resource)
			, SourceIdentity(resource)
			, Message(resource)
		{
		}
	};

	struct ShaderBuildReport
	{
		core::Array<ShaderDiagnostic> Diagnostics;
		bool bTruncated = false;
		bool bAllocationFailure = false;

		explicit ShaderBuildReport(core::MemoryResource& resource) noexcept
			: Diagnostics(resource)
		{
		}
	};

	struct ShaderDependency
	{
		core::String Identity;
		core::Hash256 ContentHash;

		explicit ShaderDependency(core::MemoryResource& resource) noexcept
			: Identity(resource)
		{
		}
	};

	struct ShaderBuildOutput
	{
		shader::Shader Artifact;
		core::Array<ShaderDependency> Dependencies;
		core::Hash256 BuildIdentity;

		explicit ShaderBuildOutput(core::MemoryResource& resource) noexcept
			: Dependencies(resource)
		{
		}

		ShaderBuildOutput(ShaderBuildOutput&&) noexcept = default;
		ShaderBuildOutput& operator=(ShaderBuildOutput&&) noexcept = default;
		ShaderBuildOutput(const ShaderBuildOutput&) = delete;
		ShaderBuildOutput& operator=(const ShaderBuildOutput&) = delete;
	};

	struct ShaderCompilerOptions
	{
		core::uint32 ShaderAbiVersion = shader::SHADER_ABI_VERSION;
	};

	struct ShaderCompilerCapabilities
	{
		bool bDxil = false;
		bool bSpirV = false;
		core::uint64 StageMask = 0;
	};

	enum class EShaderCompilerProfile : core::uint32
	{
		DxilRasterCompute66,
		SpirvRasterCompute16,
		DxilMeshRaster66,
		SpirvMeshRaster16,
		DxilRay66,
		SpirvRay16
	};

	struct ShaderCompilerProfile
	{
		EShaderCompilerProfile Profile = EShaderCompilerProfile::DxilRasterCompute66;
		core::uint32 ProfileVersion = 1;
		EShaderSourceLanguage Language = EShaderSourceLanguage::Slang;
		shader::ShaderTargetProfile Target;

		core::uint32 LanguageVersion = 2021;
		core::uint32 EngineProfileVersion = 1;
		core::uint32 ShaderAbiVersion = shader::SHADER_ABI_VERSION;

		core::uint64 StageMask = 7;
		bool bModuleCompilation = false;
		bool bLinking = false;
		bool bSpecialization = false;
		bool bGenericEntrySpecialization = false;
	};

	struct ShaderToolchainIdentity
	{
		EShaderCompiler Compiler = EShaderCompiler::Slang;
		core::uint32 AdapterVersion = 1;
		core::uint32 ShaderAbiVersion = shader::SHADER_ABI_VERSION;

		core::String SdkIdentity;
		core::String CompilerVersion;

		explicit ShaderToolchainIdentity(core::MemoryResource& resource) noexcept
			: SdkIdentity(resource)
			, CompilerVersion(resource)
		{
		}

		bool operator==(const ShaderToolchainIdentity& other) const noexcept
		{
			return Compiler == other.Compiler && AdapterVersion == other.AdapterVersion && ShaderAbiVersion == other.ShaderAbiVersion &&
				SdkIdentity == other.SdkIdentity && CompilerVersion == other.CompilerVersion;
		}
	};

} // namespace rox::shader
