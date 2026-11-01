#pragma once

#include <Cancellation.h>
#include <Result.hpp>
#include <ShaderKernel.h>
#include <SharedPtr.hpp>

namespace rox::shader
{
	struct ShaderBuilder;

	struct ShaderVariantKey
	{
		core::StringView EntryPoint;
		ShaderTargetProfile Target;
	};

	// Immutable shared storage; its resource outlives all handles and borrowed inspection spans.
	class Shader
	{
	private:
		struct Storage;
		friend struct ShaderBuilder;

	public:
		// Consumes the builder; all retained record resources must outlive the returned handle.
		[[nodiscard]] static core::Result<Shader> Build(
			ShaderBuilder builder,
			core::MemoryResource& memory,
			const ShaderLimits& limits = {},
			core::CancellationToken cancellation = {});

		// Clones the builder into resource; failure leaves the caller's builder unchanged.
		[[nodiscard]] static core::Result<Shader> Create(
			const ShaderBuilder& builder,
			core::MemoryResource& memory,
			const ShaderLimits& limits = {},
			core::CancellationToken cancellation = {});

		[[nodiscard]] static core::Result<Shader> Load(
			core::Span<const core::Byte> bytes,
			core::MemoryResource& memory,
			const ShaderLimits& limits = {},
			core::CancellationToken cancellation = {});

		Shader() noexcept;
		~Shader();
		Shader(const Shader&) noexcept;
		Shader(Shader&&) noexcept;
		Shader& operator=(const Shader&) noexcept;
		Shader& operator=(Shader&&) noexcept;

		[[nodiscard]] core::Result<core::Array<core::Byte>> Save(core::MemoryResource& memory, core::CancellationToken cancellation = {}) const;
		[[nodiscard]] core::Result<core::Array<core::Byte>> Save(EShaderFormat format, core::MemoryResource& memory, core::CancellationToken cancellation = {}) const;

		core::StringView GetName() const noexcept;
		core::Span<const ShaderProperty> GetProperties() const noexcept;
		core::Span<const ShaderPass> GetPasses() const noexcept;
		core::Span<const ShaderKernel> GetKernels() const noexcept;
		core::Span<const core::String> GetSupportedOptions() const noexcept;
		core::Span<const ShaderPropertyBinding> GetPropertyBindings() const noexcept;

		[[nodiscard]] core::Result<ShaderPropertyHandle> FindProperty(core::StringView name) const;
		[[nodiscard]] core::Result<const ShaderProperty*> GetProperty(ShaderPropertyHandle handle) const;
		[[nodiscard]] core::Result<core::uint32> FindPass(core::StringView name) const;
		[[nodiscard]] core::Result<core::uint32> FindKernel(core::StringView name) const;
		[[nodiscard]] core::Result<ShaderPassSelection> SelectPass(
			core::StringView name,
			ShaderTargetProfile target,
			const ShaderOptions& options,
			core::MemoryResource& memory) const;
		[[nodiscard]] core::Result<ShaderKernelSelection> SelectKernel(
			core::StringView name,
			ShaderTargetProfile target,
			const ShaderOptions& options,
			core::MemoryResource& memory) const;

		bool IsValid() const noexcept;
		core::uint32 GetAbiVersion() const noexcept;
		core::Hash256 GetBuildProvenance() const noexcept;
		core::Hash256 GetIdentity() const noexcept;
		core::Hash256 GetVariantCodeHash(core::uint32 index) const noexcept;
		core::Span<const ShaderEntryPoint> GetEntryPoints() const noexcept;
		core::Span<const ShaderVariant> GetVariants() const noexcept;
		core::Result<core::uint32> FindVariant(const ShaderVariantKey& key) const;

	private:
		static core::Result<Shader> publishCanonical(ShaderBuilder, core::MemoryResource&, core::CancellationToken, const core::Hash256* identity);

		core::SharedPtr<Storage> m_Storage;
	};

	struct ShaderEntrySelection
	{
		Shader Artifact;
		core::uint32 VariantIndex = 0;
	};

} // namespace rox::shader
