#include "CapturedSourceCompiler.h"

namespace rox::test
{
	using namespace core;
	using namespace shader;

	CapturedSourceCompiler::CapturedSourceCompiler(shader::IShaderCompiler& compiler) noexcept
		: m_Compiler(compiler)
	{
	}

	shader::ShaderCompilerCapabilities CapturedSourceCompiler::GetCapabilities() const noexcept
	{
		return m_Compiler.GetCapabilities();
	}

	const shader::ShaderToolchainIdentity& CapturedSourceCompiler::GetToolchainIdentity() const noexcept
	{
		return m_Compiler.GetToolchainIdentity();
	}

	core::Span<const shader::ShaderCompilerProfile> CapturedSourceCompiler::GetProfiles() const noexcept
	{
		return m_Compiler.GetProfiles();
	}

	core::Result<shader::ShaderBuildOutput> CapturedSourceCompiler::Compile(
		const shader::ShaderBuildPlan& plan,
		shader::ShaderBuildReport& report,
		core::MemoryResource& resource,
		core::CancellationToken cancellation)
	{
		using R = core::Result<shader::ShaderBuildOutput>;

		auto invalid = []()
		{
			return R::Failure(core::EErrorCode::InvalidArgument, "Test.CapturedSourceContract", "Captured alias/include queries disagree with the immutable source inventory.");
		};

		if (!plan.IsValid() || plan.GetKind() != shader::EShaderBuildPlanKind::SourceProgram)
		{
			return invalid();
		}

		const core::Span<const ShaderSourceSnapshot> sources = plan.GetSources();

		for (core::usize index = 0; index < plan.GetSourceAliasCount(); ++index)
		{
			const ShaderSourceAliasView alias = plan.GetSourceAlias(index);
			if (alias.SourceIndex >= sources.Size() || plan.FindSource(alias.Path) != &sources[alias.SourceIndex])
			{
				return invalid();
			}
		}

		for (core::usize index = 0; index < plan.GetSourceIncludeCount(); ++index)
		{
			const ShaderSourceIncludeView edge = plan.GetSourceInclude(index);
			if (edge.Requester >= sources.Size())
			{
				return invalid();
			}

			const ShaderIncludeResolution resolved = plan.ResolveInclude(sources[edge.Requester].Identity, edge.RequestedPath);
			if (edge.SourceIndex == shader::NO_CAPTURED_SHADER_SOURCE)
			{
				if (resolved.Status != shader::EShaderIncludeResolution::Missing || resolved.Source != nullptr)
				{
					return invalid();
				}
			}
			else if (edge.SourceIndex >= sources.Size() || resolved.Status != shader::EShaderIncludeResolution::Resolved || resolved.Source != &sources[edge.SourceIndex])
			{
				return invalid();
			}
		}

		return m_Compiler.Compile(plan, report, resource, cancellation);
	}
} // namespace rox::test
