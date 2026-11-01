#pragma once

#include <Array.hpp>
#include <Cancellation.h>
#include <ShaderCompilerTypes.h>
#include <ShaderModuleArtifact.h>
#include <SharedPtr.hpp>

namespace rox::shader
{
	inline constexpr core::uint32 NO_CAPTURED_SHADER_SOURCE = 0xffffffffu;

	// Indices select GetSources(); strings borrow the retained ShaderBuildPlan.
	struct ShaderSourceAliasView final
	{
		core::StringView Path;
		core::uint32 SourceIndex = NO_CAPTURED_SHADER_SOURCE;
	};

	struct ShaderSourceIncludeView final
	{
		core::uint32 Requester = NO_CAPTURED_SHADER_SOURCE;
		core::uint32 SourceIndex = NO_CAPTURED_SHADER_SOURCE;
		core::StringView RequestedPath;
	};

	enum class EShaderIncludeResolution : core::uint8
	{
		Uncaptured,
		Missing,
		Resolved
	};

	struct ShaderIncludeResolution final
	{
		EShaderIncludeResolution Status = EShaderIncludeResolution::Uncaptured;
		const ShaderSourceSnapshot* Source = nullptr;
	};

	inline constexpr core::uint32 SHADER_BUILD_INPUT_FORMAT_VERSION = 1;
	inline constexpr core::uint64 MAXIMUM_SHADER_BUILD_INPUT_BYTES = 1024ull * 1024ull * 1024ull;

	class ShaderBuildInputWriter;
	class IShaderCompiler;
	class IShaderSourceResolver;

	// Immutable captured sources; the allocating resource outlives all handles and borrowed spans.
	class ShaderBuildPlan final
	{
	public:
		static core::Result<ShaderBuildPlan> Capture(const IShaderCompiler&, const ShaderCompileRequest&, IShaderSourceResolver&, core::MemoryResource&, core::CancellationToken);
		static core::Result<ShaderBuildPlan> CaptureLink(
			const IShaderCompiler&,
			const ShaderLinkRequest&,
			core::Span<const ShaderModuleArtifact>,
			core::MemoryResource&,
			core::CancellationToken);

		ShaderBuildPlan() noexcept;
		~ShaderBuildPlan();
		ShaderBuildPlan(const ShaderBuildPlan&) noexcept;
		ShaderBuildPlan& operator=(const ShaderBuildPlan&) noexcept;
		ShaderBuildPlan(ShaderBuildPlan&&) noexcept;
		ShaderBuildPlan& operator=(ShaderBuildPlan&&) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] EShaderBuildPlanKind GetKind() const noexcept;
		[[nodiscard]] core::Hash256 GetBuildIdentity() const noexcept;
		[[nodiscard]] const ShaderToolchainIdentity& GetToolchainIdentity() const noexcept;
		[[nodiscard]] const ShaderCompileRequest& GetRequest() const noexcept;
		[[nodiscard]] core::Span<const ShaderSourceSnapshot> GetSources() const noexcept;

		// Exact canonical identity or captured alias; never normalizes paths or calls a live resolver.
		// All returned pointers/views borrow this plan. These queries allocate no memory.
		[[nodiscard]] const ShaderSourceSnapshot* FindSource(core::StringView identityOrAlias) const noexcept;
		[[nodiscard]] ShaderIncludeResolution ResolveInclude(core::StringView requester, core::StringView requestedPath) const noexcept;
		[[nodiscard]] core::usize GetSourceAliasCount() const noexcept;
		[[nodiscard]] ShaderSourceAliasView GetSourceAlias(core::usize index) const noexcept;
		[[nodiscard]] core::usize GetSourceIncludeCount() const noexcept;
		[[nodiscard]] ShaderSourceIncludeView GetSourceInclude(core::usize index) const noexcept;
		[[nodiscard]] core::Span<const ShaderDependency> GetDependencies() const noexcept;
		[[nodiscard]] const ShaderLinkRequest& GetLinkRequest() const noexcept;
		[[nodiscard]] core::Span<const ShaderModuleArtifact> GetModuleArtifacts() const noexcept;

		// Complete semantic input equality, independent of diagnostic and admission policy.
		[[nodiscard]] bool HasSameInputs(const ShaderBuildPlan& other) const noexcept;
		// Counts retained plan/module capacity for bounded service admission. Requires a valid plan.
		[[nodiscard]] core::uint64 GetRetainedByteCount() const noexcept;
		// Empty plans cannot be submitted to concurrent compilation.
		[[nodiscard]] bool CanShareAcrossThreads() const noexcept;

		// Encoding and verification require a valid plan; malformed proof bytes return failures.
		[[nodiscard]] core::Result<core::Array<core::Byte>> EncodeInputs(
			core::MemoryResource& resource,
			core::uint64 maximumBytes,
			core::CancellationToken cancellation = {}) const;
		[[nodiscard]] core::Result<void> VerifyInputs(core::Span<const core::Byte> bytes, core::uint64 maximumBytes, core::CancellationToken cancellation = {}) const;

	private:
		void writeInputsRecursive(ShaderBuildInputWriter&, core::uint32 depth) const;

	private:
		struct Storage;
		core::SharedPtr<Storage> m_Storage;
	};

} // namespace rox::shader
