#include "ShaderTestSources.h"

#include "ShaderCompilerTest.h"

#include <cstdio>

namespace rox::test
{
	using namespace core;
	using namespace shader;

	Result<ShaderSourceSnapshot> MemoryShaderSources::Resolve(StringView requester, StringView path, MemoryResource& memory, CancellationToken)
	{
		++Calls;
		if (CancelOnResolve != nullptr)
		{
			CancelOnResolve->RequestCancellation();
		}

		if (path == "shared/math.slang" || path == "kernels/shared/math.slang")
		{
			return CreateShaderSourceSnapshot(
				"kernels/shared/math.slang",
				"#include \"offset.slang\"\n"
				"#ifndef SCALE\n#define SCALE 3\n#endif\n"
				"uint Evaluate(uint value) { return value * SCALE + Offset; }\n",
				memory);
		}

		if (path == "offset.slang" || path == "kernels/shared/offset.slang")
		{
			bSawNestedInclude = requester == "kernels/shared/math.slang";
			return CreateShaderSourceSnapshot("kernels/shared/offset.slang", OffsetSource, memory);
		}

		return Result<ShaderSourceSnapshot>::Failure(EErrorCode::NotFound, "Test.SourceNotFound", "The virtual shader source does not exist.");
	}

	bool MakeComputeRequest(ShaderCompileRequest& request, MemoryResource& memory, EShaderTarget target, bool bTwoEntries)
	{
		Result<ShaderSourceSnapshot> root = CreateShaderSourceSnapshot("kernels/main.slang", COMPUTE_SOURCE, memory);
		CHECK(Succeeded(root));
		request.Root = Move(root).TakeValue();

		for (uint32 index = 0; index < (bTwoEntries ? 2u : 1u); ++index)
		{
			ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign(index == 0 ? StringView("ComputeMain") : StringView("ComputeSecond")));
			entry.Stage = EShaderStage::Compute;
			CHECK(request.EntryPoints.Add(Move(entry)));
		}

		ShaderTargetProfile profile;
		profile.Target = target;
		CHECK(request.Targets.Add(profile));
		return true;
	}

	void PrintBuildReport(const ShaderBuildReport& report)
	{
		for (const ShaderDiagnostic& diagnostic : report.Diagnostics)
		{
			std::fprintf(stderr, "%s:%u:%u: %s\n", diagnostic.SourceIdentity.CStr(), diagnostic.Line, diagnostic.Column, diagnostic.Message.CStr());
		}
	}

	bool CompileDescriptorArrayShader(EShaderTarget target, bool bRuntimeArray, MemoryResource& memory, Shader& artifact, bool bWritable, bool bSplitSets)
	{
		Result<InterfaceOwner<IShaderCompiler>> compiler = CreateShaderCompiler(EShaderCompiler::Slang, memory);
		CHECK(Succeeded(compiler));
		const StringView arraySize = bRuntimeArray ? "[]" : (bWritable ? "[64]" : "[16]");
		const StringView outputBinding = bWritable ? "128" : "33";
		const StringView outputSet = bSplitSets ? "0" : "2";
		const StringView parts[]{
			"[[vk::binding(0,2)]] ",
			bWritable ? "RWStructuredBuffer<uint> Inputs" : "StructuredBuffer<uint> Inputs",
			arraySize,
			bWritable ? " : register(u0,space2);\n" : " : register(t0,space2);\n",
			"[[vk::binding(", outputBinding, ",", outputSet,
			")]] RWByteAddressBuffer Output : register(u", outputBinding, ",space", outputSet,
			");\nstruct Selection { uint Index; };\n[[vk::binding(32,", outputSet,
			")]] ConstantBuffer<Selection> Selector : register(b32,space", outputSet,
			");\n[numthreads(1,1,1)] void CSMain() { Output.Store(0, Inputs[Selector.Index][0]); }\n"};
		String sourceText(memory);

		for (StringView part : parts)
		{
			CHECK(sourceText.Append(part));
		}

		Result<ShaderSourceSnapshot> source = CreateShaderSourceSnapshot("descriptors/arrays.slang", sourceText.View(), memory);
		CHECK(Succeeded(source));
		ShaderCompileRequest request(memory);
		request.Root = Move(source).TakeValue();
		ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign("CSMain"));
		entry.Stage = EShaderStage::Compute;
		CHECK(request.EntryPoints.Add(Move(entry)));
		ShaderTargetProfile profile;
		profile.Target = target;
		CHECK(request.Targets.Add(profile));

		MemoryShaderSources sources;
		ShaderBuildReport report(memory);
		Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(request, sources, report, memory);
		if (!compiled)
		{
			PrintBuildReport(report);
		}

		CHECK(Succeeded(compiled));
		artifact = compiled.Value().Artifact;
		return true;
	}
}
