#pragma once

#include <Cancellation.h>
#include <Result.hpp>
#include <Shader.h>
#include <ShaderKernel.h>

namespace rox::shader
{
	struct ShaderBuilder
	{
		// Copies compiler output into editable builder-owned storage before adding authored definitions.
		[[nodiscard]] static core::Result<ShaderBuilder> FromShader(const Shader& shader, core::MemoryResource& memory);

		core::String Name;

		core::Array<ShaderProperty> Properties;

		core::Array<ShaderPass> Passes;
		core::Array<ShaderKernel> Kernels;

		core::Array<core::String> Keywords;
		core::Array<ShaderPropertyBinding> PropertyBindings;

		core::uint32 AbiVersion = SHADER_ABI_VERSION;
		core::Hash256 BuildProvenance;

		core::Array<ShaderEntryPoint> EntryPoints;
		core::Array<ShaderVariant> Variants;

		explicit ShaderBuilder(core::MemoryResource& resource) noexcept
			: Name(resource)
			, Properties(resource)
			, Passes(resource)
			, Kernels(resource)
			, Keywords(resource)
			, PropertyBindings(resource)
			, EntryPoints(resource)
			, Variants(resource)
		{
		}

		[[nodiscard]] core::Result<void> AddProperty(ShaderProperty value);
		[[nodiscard]] core::Result<void> AddPass(ShaderPass value);
		[[nodiscard]] core::Result<void> AddKernel(ShaderKernel value);
		[[nodiscard]] core::Result<core::uint32> AddCompiledEntry(ShaderEntryPoint value);
		[[nodiscard]] core::Result<void> AddVariant(ShaderVariant value);
		[[nodiscard]] core::Result<void> AddKeyword(core::StringView name);
		[[nodiscard]] core::Result<void> AddPropertyBinding(ShaderPropertyBinding value);
		[[nodiscard]] core::Result<void> Validate(const ShaderLimits& limits = {}) const;
		[[nodiscard]] core::Result<core::Array<core::Byte>> Save(core::MemoryResource& memory, core::CancellationToken cancellation = {}) const;
		[[nodiscard]] core::Result<core::Array<core::Byte>> Save(EShaderFormat format, core::MemoryResource& memory, core::CancellationToken cancellation = {}) const;

		[[nodiscard]] core::Result<core::Hash256> ComputeIdentity(core::MemoryResource& memory) const;

		// Consumes this builder. All supplied allocation resources must outlive the returned Shader.
		[[nodiscard]] core::Result<Shader> Build(core::MemoryResource& memory, const ShaderLimits& limits = {}, core::CancellationToken cancellation = {}) &&;

		ShaderBuilder(ShaderBuilder&&) noexcept = default;
		ShaderBuilder& operator=(ShaderBuilder&&) noexcept = default;

		ShaderBuilder(const ShaderBuilder&) = delete;
		ShaderBuilder& operator=(const ShaderBuilder&) = delete;
	};

} // namespace rox::shader
