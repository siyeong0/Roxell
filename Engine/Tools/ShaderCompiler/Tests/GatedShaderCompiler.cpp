#include "GatedShaderCompiler.h"

#include "ShaderCompilerGate.h"

#include <ShaderCompiler.h>

namespace rox::test
{
	using namespace core;
	using namespace shader;

	GatedShaderCompiler::GatedShaderCompiler(ShaderCompilerGate& gate, core::InterfaceOwner<shader::IShaderCompiler> compiler) noexcept
		: m_Gate(gate)
		, m_Compiler(std::move(compiler))
		, m_Worker(std::this_thread::get_id())
	{
	}

	GatedShaderCompiler::~GatedShaderCompiler()
	{
		ASSERT(m_Worker == std::this_thread::get_id(), "Worker compiler destruction stays on its creating thread.");
	}

	shader::ShaderCompilerCapabilities GatedShaderCompiler::GetCapabilities() const noexcept
	{
		return m_Compiler->GetCapabilities();
	}

	const shader::ShaderToolchainIdentity& GatedShaderCompiler::GetToolchainIdentity() const noexcept
	{
		return m_Compiler->GetToolchainIdentity();
	}

	core::Span<const shader::ShaderCompilerProfile> GatedShaderCompiler::GetProfiles() const noexcept
	{
		return m_Compiler->GetProfiles();
	}

	core::Result<shader::ShaderBuildOutput> GatedShaderCompiler::Compile(
		const shader::ShaderBuildPlan& plan,
		shader::ShaderBuildReport& report,
		core::MemoryResource& resource,
		core::CancellationToken cancellation)
	{
		ASSERT(m_Worker == std::this_thread::get_id(), "Compiler execution stays on its creating worker.");
		m_Gate.Enter();

		return m_Compiler->Compile(plan, report, resource, cancellation);
	}

	core::Result<core::InterfaceOwner<shader::IShaderCompiler>> GatedShaderCompiler::Create(
		void* context,
		shader::EShaderCompiler kind,
		const shader::ShaderCompilerOptions& options,
		core::MemoryResource& resource)
	{
		using R = core::Result<core::InterfaceOwner<shader::IShaderCompiler>>;

		ASSERT(context != nullptr, "The compiler gate is retained by the test fixture.");
		core::Result<core::InterfaceOwner<IShaderCompiler>> native = shader::CreateShaderCompiler(kind, options, resource);
		if (!native)
		{
			return R::Failure(native.GetError());
		}

		InterfaceOwner<shader::IShaderCompiler> owner = core::InterfaceOwner<shader::IShaderCompiler>::Create<GatedShaderCompiler>(
			resource, *static_cast<ShaderCompilerGate*>(context), std::move(native).TakeValue());
		if (!owner)
		{
			return R::Failure(core::EErrorCode::ExternalFailure, "ShaderTest.Allocation", "The gated compiler allocation failed.");
		}

		return R::Success(std::move(owner));
	}
} // namespace rox::test
