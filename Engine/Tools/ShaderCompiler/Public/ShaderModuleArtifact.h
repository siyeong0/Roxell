#pragma once

#include <ShaderCompilerTypes.h>
#include <SharedPtr.hpp>

namespace rox::shader
{
	enum class EShaderLinkArgumentKind : core::uint32
	{
		Type,
		UInt32
	};

	inline constexpr core::uint32 MAXIMUM_SHADER_LINK_ARGUMENTS = 64;

	struct ShaderModuleSpecializationParameter
	{
		EShaderLinkArgumentKind Kind = EShaderLinkArgumentKind::Type;
		core::String Name;

		explicit ShaderModuleSpecializationParameter(core::MemoryResource& resource) noexcept
			: Name(resource)
		{
		}
	};

	struct ShaderModuleSelection
	{
		core::StringView ModuleName;
	};

	struct ShaderModuleRecord
	{
		core::String Name;
		core::String SourceIdentity;
		core::Hash256 IRHash;
		core::uint64 ByteCount = 0;

		explicit ShaderModuleRecord(core::MemoryResource& resource) noexcept
			: Name(resource)
			, SourceIdentity(resource)
		{
		}
	};

	struct ShaderModuleEntryRecord
	{
		core::uint32 ModuleIndex = 0;
		core::String Name;
		shader::EShaderStage Stage = shader::EShaderStage::Compute;
		core::Array<ShaderModuleSpecializationParameter> SpecializationParameters;

		explicit ShaderModuleEntryRecord(core::MemoryResource& resource) noexcept
			: Name(resource)
			, SpecializationParameters(resource)
		{
		}
	};

	struct ShaderLinkArgument
	{
		EShaderLinkArgumentKind Kind = EShaderLinkArgumentKind::Type;
		// Root module in PrepareLink's ordered artifact inputs.
		core::uint32 TypeModuleIndex = 0;
		core::String TypeName;
		core::uint32 UInt32Value = 0;

		explicit ShaderLinkArgument(core::MemoryResource& resource) noexcept
			: TypeName(resource)
		{
		}
	};

	struct ShaderLinkEntrySelection
	{
		// Index in PrepareLink's ordered artifact inputs; selects that artifact's root module.
		core::uint32 ModuleIndex = 0;
		core::String Name;
		shader::EShaderStage Stage = shader::EShaderStage::Compute;
		core::Array<ShaderLinkArgument> Arguments;

		explicit ShaderLinkEntrySelection(core::MemoryResource& resource) noexcept
			: Name(resource)
			, Arguments(resource)
		{
		}
	};

	struct ShaderLinkRequest
	{
		core::Array<ShaderLinkEntrySelection> EntryPoints;
		core::Array<shader::ShaderTargetProfile> Targets;
		EShaderOptimization Optimization = EShaderOptimization::Default;
		EShaderDebugInfo DebugInfo = EShaderDebugInfo::None;
		shader::EShaderMatrixOrder MatrixOrder = shader::EShaderMatrixOrder::RowMajor;
		bool bWarningsAsErrors = false;
		bool bPreciseFloatingPoint = false;
		ShaderBuildLimits Limits;

		explicit ShaderLinkRequest(core::MemoryResource& resource) noexcept
			: EntryPoints(resource)
			, Targets(resource)
		{
		}
	};

	class ShaderModuleStorage;

	class ShaderBuildPlan;

	// Immutable tool-only IR. The allocating resources outlive all handles and borrowed spans.
	class ShaderModuleArtifact final
	{
	public:
		// Every record, string and byte array uses resource. Failure leaves the input arrays unconsumed.
		[[nodiscard]] static core::Result<ShaderModuleArtifact> Create(
			const ShaderBuildPlan& sourcePlan,
			core::StringView moduleName,
			core::uint32 rootModule,
			core::Array<ShaderModuleRecord>&& modules,
			core::Array<core::Array<core::Byte>>&& moduleBytes,
			core::Array<ShaderModuleEntryRecord>&& entries,
			core::MemoryResource& resource);
		ShaderModuleArtifact() noexcept;
		~ShaderModuleArtifact();
		ShaderModuleArtifact(const ShaderModuleArtifact&) noexcept;
		ShaderModuleArtifact& operator=(const ShaderModuleArtifact&) noexcept;
		ShaderModuleArtifact(ShaderModuleArtifact&&) noexcept;
		ShaderModuleArtifact& operator=(ShaderModuleArtifact&&) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] const ShaderBuildPlan& GetSourcePlan() const noexcept;
		[[nodiscard]] bool SharesStorageWith(const ShaderModuleArtifact& other) const noexcept;
		[[nodiscard]] bool HasSameInputs(const ShaderModuleArtifact& other) const noexcept;
		[[nodiscard]] bool CanShareAcrossThreads() const noexcept;
		[[nodiscard]] core::uint64 GetRetainedByteCount() const noexcept;
		[[nodiscard]] core::Hash256 GetModuleIdentity() const noexcept;
		[[nodiscard]] const ShaderToolchainIdentity& GetToolchainIdentity() const noexcept;
		[[nodiscard]] core::uint32 GetRootModuleIndex() const noexcept;
		[[nodiscard]] core::Span<const ShaderModuleRecord> GetModules() const noexcept;
		[[nodiscard]] core::Span<const core::Byte> GetModuleBytes(core::uint32 index) const noexcept;
		[[nodiscard]] core::Span<const ShaderModuleEntryRecord> GetEntryPoints() const noexcept;
		[[nodiscard]] core::Span<const ShaderDependency> GetDependencies() const noexcept;

	private:
		core::SharedPtr<ShaderModuleStorage> m_Storage;
	};

} // namespace rox::shader
