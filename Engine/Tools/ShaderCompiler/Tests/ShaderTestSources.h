#pragma once

#include <Cancellation.h>
#include <ShaderCompiler.h>

namespace rox::test
{
	inline constexpr char COMPUTE_SOURCE[] =
		"#include \"shared/math.slang\"\n"
		"[[vk::binding(0, 0)]] RWStructuredBuffer<uint> Output : register(u0);\n"
		"[numthreads(8, 1, 1)] void ComputeMain(uint3 id : SV_DispatchThreadID)\n"
		"{ Output[id.x] = Evaluate(Output[id.x]); }\n"
		"[numthreads(8, 1, 1)] void ComputeSecond(uint3 id : SV_DispatchThreadID)\n"
		"{ Output[id.x] = Evaluate(Output[id.x]) + 1; }\n";

	class MemoryShaderSources final : public shader::IShaderSourceResolver
	{
	public:
		core::Result<shader::ShaderSourceSnapshot> Resolve(
			core::StringView requester,
			core::StringView path,
			core::MemoryResource& memory,
			core::CancellationToken cancellation) override;

		core::StringView OffsetSource = "static const uint Offset = 7;\n";
		core::CancellationSource* CancelOnResolve = nullptr;
		core::uint32 Calls = 0;
		bool bSawNestedInclude = false;
	};

	bool MakeComputeRequest(shader::ShaderCompileRequest& request, core::MemoryResource& memory, shader::EShaderTarget target, bool bTwoEntries = false);
	void PrintBuildReport(const shader::ShaderBuildReport& report);
	bool CompileDescriptorArrayShader(
		shader::EShaderTarget target,
		bool bRuntimeArray,
		core::MemoryResource& memory,
		shader::Shader& artifact,
		bool bWritable = false,
		bool bSplitSets = false);
}
