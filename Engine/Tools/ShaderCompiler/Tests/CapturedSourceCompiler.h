#pragma once

#include <ShaderCompiler.h>

namespace rox::test
{
	class CapturedSourceCompiler final : public shader::IShaderCompiler
	{
	public:
		explicit CapturedSourceCompiler(shader::IShaderCompiler& compiler) noexcept;
		shader::ShaderCompilerCapabilities GetCapabilities() const noexcept override;
		const shader::ShaderToolchainIdentity& GetToolchainIdentity() const noexcept override;
		core::Span<const shader::ShaderCompilerProfile> GetProfiles() const noexcept override;
		core::Result<shader::ShaderBuildOutput> Compile(
			const shader::ShaderBuildPlan& plan,
			shader::ShaderBuildReport& report,
			core::MemoryResource& resource,
			core::CancellationToken cancellation = {}) override;

	private:
		shader::IShaderCompiler& m_Compiler;
	};

} // namespace rox::test
