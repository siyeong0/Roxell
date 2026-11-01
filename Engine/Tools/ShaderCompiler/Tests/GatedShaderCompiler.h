#pragma once

#include <ShaderCompiler.h>

#include <thread>

namespace rox::test
{
	class ShaderCompilerGate;

	class GatedShaderCompiler final : public shader::IShaderCompiler
	{
	public:
		GatedShaderCompiler(ShaderCompilerGate& gate, core::InterfaceOwner<shader::IShaderCompiler> compiler) noexcept;
		~GatedShaderCompiler() override;
		shader::ShaderCompilerCapabilities GetCapabilities() const noexcept override;
		const shader::ShaderToolchainIdentity& GetToolchainIdentity() const noexcept override;
		core::Span<const shader::ShaderCompilerProfile> GetProfiles() const noexcept override;
		core::Result<shader::ShaderBuildOutput> Compile(
			const shader::ShaderBuildPlan& plan,
			shader::ShaderBuildReport& report,
			core::MemoryResource& resource,
			core::CancellationToken cancellation = {}) override;
		static core::Result<core::InterfaceOwner<shader::IShaderCompiler>> Create(
			void* context,
			shader::EShaderCompiler kind,
			const shader::ShaderCompilerOptions& options,
			core::MemoryResource& resource);

	private:
		ShaderCompilerGate& m_Gate;
		core::InterfaceOwner<shader::IShaderCompiler> m_Compiler;
		std::thread::id m_Worker;
	};

} // namespace rox::test
