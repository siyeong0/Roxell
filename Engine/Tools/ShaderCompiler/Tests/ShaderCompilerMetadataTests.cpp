#include "ShaderTestSources.h"

#include <ByteReader.h>
#include <ShaderBuilder.h>
#include <ShaderLayout.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ShaderCompilerTest.h"

namespace
{
	using namespace rox;
	using namespace core;
	using namespace shader;

	bool hasSpirvExecutionMode(core::Span<const core::Byte> code, core::uint32 mode)
	{
		core::ByteReader reader(code);
		if (!reader.CanRead(20))
		{
			return false;
		}

		(void)reader.ReadBytes(20);

		while (reader.CanRead(4))
		{
			const uint32 instruction = reader.ReadU32LE();
			const uint32 words = instruction >> 16;
			if (words == 0 || !reader.CanRead(core::usize(words - 1) * 4))
			{
				return false;
			}

			const Span<const Byte> operands = reader.ReadBytes(core::usize(words - 1) * 4);
			if ((instruction & 0xffffu) == 16 && words == 3)
			{
				core::ByteReader values(operands);
				(void)values.ReadU32LE();
				if (values.ReadU32LE() == mode)
				{
					return true;
				}
			}
		}

		return false;
	}

	bool compile(
		shader::IShaderCompiler& compiler,
		shader::EShaderTarget target,
		core::MemoryResource& memory,
		bool bMismatchedPixel,
		shader::Shader& output,
		bool bPackingMismatch = false)
	{
		const core::StringView source = bPackingMismatch ? core::StringView(
															"struct V { float4 Position:SV_Position; float2 UV:TEXCOORD0; };\n"
															"V VSMain(uint id:SV_VertexID) { V v; v.Position=float4(id&1,id>>1,0,1);\n"
															"v.UV=float2(id,0); return v; }\n"
															"float4 PSMain(float2 uv:TEXCOORD0):SV_Target0 { return float4(uv,0,1); }\n")
			: bMismatchedPixel							 ? core::StringView(
															"struct V { float4 Position:SV_Position; noperspective float2 UV:TEXCOORD0;\n"
															"nointerpolation uint Tag:TEXCOORD1; };\n"
															"V VSMain(uint id:SV_VertexID) { V v; v.Position=float4(id&1,id>>1,0,1);\n"
															"v.UV=float2(id,0); v.Tag=id; return v; }\n"
															"float4 PSMain(noperspective float3 uv:TEXCOORD0):SV_Target0 { return float4(uv,1); "
															"}\n")
														: core::StringView(
															"struct V { float4 Position:SV_Position; noperspective float2 UV:TEXCOORD0;\n"
															"nointerpolation uint Tag:TEXCOORD1; };\n"
															"V VSMain(uint id:SV_VertexID) { V v; v.Position=float4(id&1,id>>1,0,1);\n"
															"v.UV=float2(id,0); v.Tag=id; return v; }\n"
															"float4 PSMain(V v):SV_Target0 { return float4(v.UV+v.Position.xy,float(v.Tag&1),1); "
															"}\n");
		shader::ShaderCompileRequest request(memory);
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot("metadata/stages.hlsl", source, memory);
		CHECK(test::Succeeded(snapshot));
		request.Root = std::move(snapshot).TakeValue();
		shader::ShaderEntryPoint vertex(memory);
		shader::ShaderEntryPoint pixel(memory);
		CHECK(vertex.Name.Assign("VSMain") && pixel.Name.Assign("PSMain"));
		vertex.Stage = shader::EShaderStage::Vertex;
		pixel.Stage = shader::EShaderStage::Pixel;
		CHECK(request.EntryPoints.Add(std::move(vertex)) && request.EntryPoints.Add(std::move(pixel)));
		shader::ShaderTargetProfile profile;
		profile.Target = target;
		CHECK(request.Targets.Add(profile));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> compiled = compiler.Compile(request, resolver, report, memory);
		if (!compiled)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(compiled));
		output = compiled.Value().Artifact;

		return true;
	}

	bool testCompiler(shader::EShaderCompiler kind, shader::EShaderTarget target, core::MemoryResource& memory)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(kind, options, memory);
		CHECK(test::Succeeded(compiler));
		CHECK(compiler.Value()->GetToolchainIdentity().ShaderAbiVersion == shader::SHADER_ABI_VERSION);

		for (const shader::ShaderCompilerProfile& profile : compiler.Value()->GetProfiles())
		{
			CHECK(profile.ShaderAbiVersion == shader::SHADER_ABI_VERSION);
			const bool bRasterCompute = (profile.StageMask & (core::uint64{1} << static_cast<core::uint32>(shader::EShaderStage::Compute))) != 0;
			const bool bModules = bRasterCompute && kind == shader::EShaderCompiler::Slang && profile.Language == shader::EShaderSourceLanguage::Slang;
			CHECK(profile.bModuleCompilation == bModules && profile.bLinking == bModules);
			CHECK(profile.bGenericEntrySpecialization == bModules);
			CHECK(profile.bSpecialization == (bRasterCompute && kind == shader::EShaderCompiler::Slang && profile.Target.Target == shader::EShaderTarget::SpirV));
		}

		shader::Shader artifact;
		CHECK(compile(*compiler.Value(), target, memory, false, artifact));
		CHECK(artifact.GetAbiVersion() == shader::SHADER_ABI_VERSION);
		shader::ShaderTargetProfile profile;
		profile.Target = target;
		core::Result<core::uint32> vertexIndex = artifact.FindVariant({"VSMain", profile});
		core::Result<core::uint32> pixelIndex = artifact.FindVariant({"PSMain", profile});
		CHECK(vertexIndex && pixelIndex);
		const shader::ShaderVariant& vertex = artifact.GetVariants()[vertexIndex.Value()];
		const shader::ShaderVariant& pixel = artifact.GetVariants()[pixelIndex.Value()];
		CHECK(vertex.bStageIOComplete && pixel.bStageIOComplete);
		bool bVertexIndex = false;
		bool bBaseVertex = false;

		for (const shader::ShaderStageIO& input : vertex.StageInputs)
		{
			bVertexIndex |= input.Builtin == shader::EShaderBuiltin::VertexIndex;
			bBaseVertex |= input.Builtin == shader::EShaderBuiltin::BaseVertex;
		}

		CHECK(bVertexIndex);
		if (kind == shader::EShaderCompiler::Slang && target == shader::EShaderTarget::SpirV)
		{
			CHECK(bBaseVertex);
		}

		bool bUV = false;
		bool bTag = false;
		bool bPixelPosition = false;

		for (const shader::ShaderStageIO& input : pixel.StageInputs)
		{
			bPixelPosition |= input.Builtin == shader::EShaderBuiltin::Position;
			if (input.Builtin != shader::EShaderBuiltin::None)
			{
				continue;
			}

			if (input.Scalar == shader::EShaderScalar::Float && input.VectorWidth == 2)
			{
				bUV = input.Interpolation == shader::EShaderInterpolation::NoPerspective;
			}

			if (input.Scalar == shader::EShaderScalar::UnsignedInteger && input.VectorWidth == 1)
			{
				bTag = input.Interpolation == shader::EShaderInterpolation::Flat;
			}
		}

		CHECK(bUV && bTag && bPixelPosition);
		shader::ShaderEntrySelection selections[]{{artifact, vertexIndex.Value()}, {artifact, pixelIndex.Value()}};
		core::Result<ShaderLayout> linked = shader::ShaderLayout::Create(selections, memory);
		CHECK(test::Succeeded(linked));
		CHECK(linked.Value().GetStageIOStatus() == shader::EShaderStageIOStatus::Verified);
		core::Result<core::Array<core::Byte>> encoded = artifact.Save(memory);
		CHECK(test::Succeeded(encoded));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == artifact.GetIdentity());
		shader::Shader incompatible;
		CHECK(compile(*compiler.Value(), target, memory, true, incompatible));
		core::Result<core::uint32> otherPixel = incompatible.FindVariant({"PSMain", profile});
		CHECK(test::Succeeded(otherPixel));
		selections[1] = {incompatible, otherPixel.Value()};
		CHECK(!shader::ShaderLayout::Create(selections, memory));
		if (target == shader::EShaderTarget::Dxil)
		{
			shader::Shader packingMismatch;
			CHECK(compile(*compiler.Value(), target, memory, false, packingMismatch, true));
			core::Result<core::uint32> packedVertex = packingMismatch.FindVariant({"VSMain", profile});
			core::Result<core::uint32> packedPixel = packingMismatch.FindVariant({"PSMain", profile});
			CHECK(packedVertex && packedPixel);
			selections[0] = {packingMismatch, packedVertex.Value()};
			selections[1] = {packingMismatch, packedPixel.Value()};
			CHECK(!shader::ShaderLayout::Create(selections, memory));
		}

		return true;
	}

	bool testDepthOnlyCompiler(shader::EShaderCompiler kind, shader::EShaderTarget target, core::MemoryResource& memory)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(kind, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot(
			"metadata/depth.hlsl",
			"float4 VSMain(uint id:SV_VertexID):SV_Position { return float4(id&1,id>>1,0.5,1); }\n"
			"float PSDepth(float4 position:SV_Position):SV_Depth { return position.z; }\n"
			"float PSGreater():SV_DepthGreaterEqual { return 0.75; }\n"
			"float PSLess():SV_DepthLessEqual { return 0.25; }\n"
			"void PSMask(float4 position:SV_Position) { if (position.x<1) discard; }\n"
			"struct Output { float4 Color:SV_Target0; float Depth:SV_Depth; };\n"
			"Output PSColorDepth(float4 position:SV_Position) { Output value;\n"
			"value.Color=float4(position.xy,0,1); value.Depth=position.z; return value; }\n",
			memory);
		CHECK(test::Succeeded(snapshot));
		request.Root = std::move(snapshot).TakeValue();
		const core::StringView names[]{"VSMain", "PSDepth", "PSMask", "PSColorDepth", "PSGreater", "PSLess"};

		for (core::uint32 index = 0; index < std::size(names); ++index)
		{
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign(names[index]));
			entry.Stage = index == 0 ? shader::EShaderStage::Vertex : shader::EShaderStage::Pixel;
			CHECK(request.EntryPoints.Add(std::move(entry)));
		}

		shader::ShaderTargetProfile profile;
		profile.Target = target;
		CHECK(request.Targets.Add(profile));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(request, resolver, report, memory);
		if (!compiled)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(compiled));
		const shader::Shader artifact = compiled.Value().Artifact;
		core::Result<core::uint32> vertexIndex = artifact.FindVariant({names[0], profile});
		CHECK(test::Succeeded(vertexIndex));
		const shader::ShaderEntrySelection vertex{artifact, vertexIndex.Value()};
		core::Result<ShaderLayout> vertexOnly = shader::ShaderLayout::Create({&vertex, 1}, memory);
		CHECK(test::Succeeded(vertexOnly));
		CHECK(vertexOnly.Value().GetStageIOStatus() == shader::EShaderStageIOStatus::Verified);
		CHECK(vertexOnly.Value().GetSelections().Size() == 1);

		for (core::uint32 index = 1; index < std::size(names); ++index)
		{
			core::Result<core::uint32> pixelIndex = artifact.FindVariant({names[index], profile});
			CHECK(test::Succeeded(pixelIndex));
			const shader::ShaderVariant& pixel = artifact.GetVariants()[pixelIndex.Value()];
			CHECK(pixel.bStageIOComplete);
			const core::uint64 demote = static_cast<core::uint64>(shader::EShaderFeature::DemoteToHelperInvocation);
			CHECK(((pixel.Requirements.Features & demote) != 0) == (index == 2 && target == shader::EShaderTarget::SpirV));
			CHECK(pixel.StageOutputs.Size() == (index == 2 ? 0 : index == 3 ? 2 : 1));
			bool bDepthOutput = false;

			for (const shader::ShaderStageIO& output : pixel.StageOutputs)
			{
				if (output.Builtin == shader::EShaderBuiltin::FragDepth)
				{
					CHECK(
						output.Scalar == shader::EShaderScalar::Float && output.ScalarWidth == 32 && output.VectorWidth == 1 && output.ArrayCount == 1 &&
						output.Location == shader::NO_STAGE_LOCATION);
					bDepthOutput = true;
				}
			}

			CHECK(bDepthOutput == (index != 2));
			if (target == shader::EShaderTarget::SpirV && index >= 4)
			{
				CHECK(hasSpirvExecutionMode(pixel.Bytecode.View(), 12));
				CHECK(hasSpirvExecutionMode(pixel.Bytecode.View(), index == 4 ? 14 : 15));
				CHECK(!hasSpirvExecutionMode(pixel.Bytecode.View(), index == 4 ? 15 : 14));
			}

			const shader::ShaderEntrySelection selections[]{vertex, {artifact, pixelIndex.Value()}};
			core::Result<ShaderLayout> linked = shader::ShaderLayout::Create(selections, memory);
			CHECK(test::Succeeded(linked));
			CHECK(linked.Value().GetStageIOStatus() == shader::EShaderStageIOStatus::Verified);
		}

		core::Result<core::Array<core::Byte>> encoded = artifact.Save(memory);
		CHECK(test::Succeeded(encoded));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == artifact.GetIdentity());

		return true;
	}

	bool testDemoteCompilerProfiles(core::MemoryResource& memory)
	{
		struct ProfileRevision
		{
			core::uint32 Abi;
			core::uint32 Adapter;
			core::uint32 SpirV;
			core::uint32 Dxil;
		};

		const ProfileRevision revisions[]{{shader::SHADER_ABI_VERSION, 2, 1, 1}};

		for (const ProfileRevision& revision : revisions)
		{
			shader::ShaderCompilerOptions options;
			options.ShaderAbiVersion = revision.Abi;
			core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
			CHECK(test::Succeeded(compiler));
			CHECK(compiler.Value()->GetToolchainIdentity().AdapterVersion == revision.Adapter);
			bool bSpirVPixel = false;

			for (const shader::ShaderCompilerProfile& profile : compiler.Value()->GetProfiles())
			{
				CHECK(profile.ShaderAbiVersion == revision.Abi);
				const core::uint64 pixel = core::uint64{1} << static_cast<core::uint32>(shader::EShaderStage::Pixel);
				if ((profile.StageMask & pixel) != 0)
				{
					CHECK(profile.ProfileVersion == (profile.Target.Target == shader::EShaderTarget::SpirV ? revision.SpirV : revision.Dxil));
					bSpirVPixel |= profile.Target.Target == shader::EShaderTarget::SpirV;
				}
				else
				{
					CHECK(profile.ProfileVersion == 1);
				}
			}

			CHECK(bSpirVPixel);
			shader::ShaderCompileRequest request(memory);
			request.Language = shader::EShaderSourceLanguage::Slang;
			core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot(
				"metadata/demote.slang",
				"void PSMask(float4 position:SV_Position) { if (position.x<1) discard; }\n"
				"float PSDepth(float4 position:SV_Position):SV_Depth { return position.z; }\n"
				"float PSGreater():SV_DepthGreaterEqual { return 0.75; }\n"
				"float PSLess():SV_DepthLessEqual { return 0.25; }\n",
				memory);
			CHECK(test::Succeeded(snapshot));
			request.Root = std::move(snapshot).TakeValue();
			const core::StringView names[]{"PSMask", "PSDepth", "PSGreater", "PSLess"};

			for (core::StringView name : names)
			{
				shader::ShaderEntryPoint entry(memory);
				CHECK(entry.Name.Assign(name));
				entry.Stage = shader::EShaderStage::Pixel;
				CHECK(request.EntryPoints.Add(std::move(entry)));
			}

			shader::ShaderTargetProfile target;
			target.Target = shader::EShaderTarget::SpirV;
			CHECK(request.Targets.Add(target));
			test::MemoryShaderSources resolver;
			shader::ShaderBuildReport report(memory);
			core::Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(request, resolver, report, memory);
			if (!compiled)
			{
				test::PrintBuildReport(report);
			}

			CHECK(test::Succeeded(compiled));
			const shader::Shader artifact = compiled.Value().Artifact;
			CHECK(artifact.GetAbiVersion() == revision.Abi && artifact.GetVariants().Size() == std::size(names));

			for (core::uint32 index = 0; index < std::size(names); ++index)
			{
				core::Result<core::uint32> found = artifact.FindVariant({names[index], target});
				CHECK(test::Succeeded(found));
				const shader::ShaderVariant& variant = artifact.GetVariants()[found.Value()];
				const core::uint64 expected = index == 0 ? static_cast<core::uint64>(shader::EShaderFeature::DemoteToHelperInvocation) : core::uint64{0};
				CHECK(variant.Requirements.Features == expected && variant.RequiredCapabilities == 0);
				CHECK(variant.bStageIOComplete && variant.StageOutputs.Size() == (index == 0 ? 0 : 1));
				if (index != 0)
				{
					CHECK(variant.StageOutputs[0].Builtin == shader::EShaderBuiltin::FragDepth);
				}
			}

			core::Result<core::Array<core::Byte>> encoded = artifact.Save(memory);
			CHECK(test::Succeeded(encoded));
			core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
			CHECK(decoded && decoded.Value().GetIdentity() == artifact.GetIdentity());
		}

		return true;
	}

	bool testInt64AtomicRequirements(core::MemoryResource& memory)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot(
			"metadata/atomic.hlsl",
			"RWStructuredBuffer<uint64_t> Counter;\n"
			"[numthreads(1,1,1)] void CSMain() { uint64_t old; "
			"InterlockedAdd(Counter[0],uint64_t(1),old); }\n",
			memory);
		CHECK(test::Succeeded(snapshot));
		request.Root = std::move(snapshot).TakeValue();
		shader::ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign("CSMain"));
		entry.Stage = shader::EShaderStage::Compute;
		CHECK(request.EntryPoints.Add(std::move(entry)));
		shader::ShaderTargetProfile profile;
		profile.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(profile));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(request, resolver, report, memory);
		if (!compiled)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(compiled));
		const shader::ShaderVariant& variant = compiled.Value().Artifact.GetVariants()[0];
		CHECK((variant.Requirements.Features & static_cast<core::uint64>(shader::EShaderFeature::Int64Atomics)) != 0);

		return true;
	}

	bool testSampleIndexCompiler(core::MemoryResource& memory)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot(
			"metadata/sample.hlsl",
			"float4 PSMain(uint sampleIndex:SV_SampleIndex):SV_Target0 { return float4(sampleIndex,0,0,1); "
			"}\n",
			memory);
		CHECK(test::Succeeded(snapshot));
		request.Root = std::move(snapshot).TakeValue();
		shader::ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign("PSMain"));
		entry.Stage = shader::EShaderStage::Pixel;
		CHECK(request.EntryPoints.Add(std::move(entry)));
		shader::ShaderTargetProfile target;
		target.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(target));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(request, resolver, report, memory);
		if (!compiled)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(compiled));
		const shader::ShaderVariant& variant = compiled.Value().Artifact.GetVariants()[0];
		CHECK(variant.bStageIOComplete && variant.StageInputs.Size() == 1);
		CHECK(variant.StageInputs[0].Builtin == shader::EShaderBuiltin::SampleIndex);
		CHECK(variant.Requirements.Features == static_cast<core::uint64>(shader::EShaderFeature::SampleRateShading));
		CHECK(variant.RequiredCapabilities == 0);
		core::Result<core::Array<core::Byte>> encoded = compiled.Value().Artifact.Save(memory);
		CHECK(test::Succeeded(encoded));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == compiled.Value().Artifact.GetIdentity());

		return true;
	}

	bool testSampledStencilFormat(core::MemoryResource& memory)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot(
			"metadata/stencil-format.hlsl",
			"[[vk::binding(0,0)]] [[vk::image_format(\"unknown\")]] Texture2DArray<uint> StencilInput;\n"
			"[[vk::binding(1,0)]] [[vk::image_format(\"r32ui\")]] RWTexture2D<uint> Output;\n"
			"[numthreads(1,1,1)] void CSMain(uint3 id:SV_DispatchThreadID) { "
			"Output[id.xy]=StencilInput.Load(int4(id.xy,0,0)); }\n",
			memory);
		CHECK(test::Succeeded(snapshot));
		request.Root = std::move(snapshot).TakeValue();
		shader::ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign("CSMain"));
		entry.Stage = shader::EShaderStage::Compute;
		CHECK(request.EntryPoints.Add(std::move(entry)));
		shader::ShaderTargetProfile target;
		target.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(target));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(request, resolver, report, memory);
		if (!compiled)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(compiled));
		const shader::ShaderVariant& variant = compiled.Value().Artifact.GetVariants()[0];
		core::ByteReader reader(variant.Bytecode.View());
		CHECK(reader.CanRead(20));
		(void)reader.ReadBytes(20);
		core::uint32 sampledCount = 0;
		core::uint32 storageCount = 0;

		while (reader.CanRead(4))
		{
			const uint32 instruction = reader.ReadU32LE();
			const uint32 words = instruction >> 16;
			CHECK(words != 0 && reader.CanRead(core::usize(words - 1) * 4));
			core::ByteReader operands(reader.ReadBytes(core::usize(words - 1) * 4));
			if ((instruction & 0xffffu) == 25)
			{
				CHECK(words >= 9);
				(void)operands.ReadBytes(6 * 4);
				const uint32 sampled = operands.ReadU32LE();
				const uint32 format = operands.ReadU32LE();
				CHECK(sampled == 1 || sampled == 2);
				CHECK(format == (sampled == 1 ? 0u : 33u));
				sampledCount += sampled == 1;
				storageCount += sampled == 2;
			}

			if ((instruction & 0xffffu) == 17)
			{
				CHECK(words == 2);
				const uint32 capability = operands.ReadU32LE();
				CHECK(capability != 55 && capability != 56);
			}
		}

		CHECK(sampledCount == 1 && storageCount == 1);

		return true;
	}

	bool testTypedUavLoadRequirements(shader::EShaderCompiler kind, core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(kind, {}, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot(
			"metadata/typed-uav.hlsl", R"hlsl(
RWTexture2D<float4> Color : register(u0);
RWTexture2D<float> Scalar : register(u1);
[numthreads(1,1,1)] void ReadColor() { Color[uint2(0,0)] += float4(1,2,3,0); }
[numthreads(1,1,1)] void WriteColor() { Color[uint2(0,0)] = float4(1,2,3,4); }
[numthreads(1,1,1)] void ReadScalar() { Scalar[uint2(0,0)] += 1; }
			)hlsl",
			memory);
		CHECK(test::Succeeded(source));
		request.Root = std::move(source).TakeValue();
		const core::StringView names[]{"ReadColor", "WriteColor", "ReadScalar"};

		for (core::StringView name : names)
		{
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign(name));
			entry.Stage = shader::EShaderStage::Compute;
			CHECK(request.EntryPoints.Add(std::move(entry)));
		}

		shader::ShaderTargetProfile target;
		target.Target = shader::EShaderTarget::Dxil;
		CHECK(request.Targets.Add(target));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(request, resolver, report, memory);
		if (!compiled)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(compiled));
		core::Result<core::Array<core::Byte>> encoded = compiled.Value().Artifact.Save(memory);
		CHECK(test::Succeeded(encoded));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == compiled.Value().Artifact.GetIdentity());

		for (core::uint32 index = 0; index < std::size(names); ++index)
		{
			core::Result<core::uint32> selected = decoded.Value().FindVariant({names[index], target});
			CHECK(test::Succeeded(selected));
			const shader::ShaderVariant& variant = decoded.Value().GetVariants()[selected.Value()];
			const core::uint64 expected = index == 0 ? static_cast<core::uint64>(shader::EShaderFeature::TypedUavLoadAdditionalFormats) : 0;
			CHECK(variant.Requirements.Features == expected && variant.RequiredCapabilities == 0);
			shader::ShaderEntrySelection selection{decoded.Value(), selected.Value()};
			core::Result<ShaderLayout> linked = shader::ShaderLayout::Create({&selection, 1}, memory);
			CHECK(linked && linked.Value().GetFeatureRequirements().Features == expected);
		}

		return true;
	}

	bool testImageGatherExtended(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(compiler));

		for (const bool bDynamic : {false, true})
		{
			shader::ShaderCompileRequest request(memory);
			const core::StringView source = bDynamic ? R"(
[[vk::binding(0,0)]] Texture2D<float4> Input;
[[vk::binding(1,0)]] SamplerState InputSampler;
[[vk::binding(2,0)]] RWStructuredBuffer<float4> Output;
[numthreads(1,1,1)] void CSMain(uint3 id:SV_DispatchThreadID)
{
    Output[id.x]=Input.GatherRed(InputSampler,float2(.375,.625),int2(id.xy&1));
				})"
													: R"(
[[vk::binding(0,0)]] Texture2D<float4> Input;
[[vk::binding(1,0)]] SamplerState InputSampler;
[[vk::binding(2,0)]] RWStructuredBuffer<float4> Output;
[numthreads(1,1,1)] void CSMain(uint3 id:SV_DispatchThreadID)
{
    Output[id.x]=Input.GatherRed(InputSampler,float2(.375,.625),int2(1,-1));
			})";
			core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot(
				bDynamic ? "metadata/gather-dynamic.slang" : "metadata/gather-constant.slang", source, memory);
			CHECK(test::Succeeded(snapshot));
			request.Root = std::move(snapshot).TakeValue();
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign("CSMain"));
			entry.Stage = shader::EShaderStage::Compute;
			CHECK(request.EntryPoints.Add(std::move(entry)));
			shader::ShaderTargetProfile target;
			target.Target = shader::EShaderTarget::SpirV;
			CHECK(request.Targets.Add(target));
			test::MemoryShaderSources resolver;
			shader::ShaderBuildReport report(memory);
			core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, resolver, report, memory);
			if (!built)
			{
				test::PrintBuildReport(report);
			}

			CHECK(test::Succeeded(built));
			core::Result<core::Array<core::Byte>> encoded = built.Value().Artifact.Save(memory);
			CHECK(test::Succeeded(encoded));
			core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
			CHECK(decoded && decoded.Value().GetIdentity() == built.Value().Artifact.GetIdentity());
			const shader::ShaderVariant& variant = decoded.Value().GetVariants()[0];
			const core::uint64 expected = bDynamic ? static_cast<core::uint64>(shader::EShaderFeature::ImageGatherExtended) : 0;
			CHECK(variant.Requirements.Features == expected && variant.RequiredCapabilities == 0);
			shader::ShaderEntrySelection selection{decoded.Value(), 0};
			core::Result<ShaderLayout> linked = shader::ShaderLayout::Create({&selection, 1}, memory);
			CHECK(linked && linked.Value().GetFeatureRequirements().Features == expected);
			core::ByteReader words(variant.Bytecode.View());
			CHECK(words.CanRead(20));
			(void)words.ReadBytes(20);
			bool bNativeFeature = false;

			while (words.CanRead(4))
			{
				const uint32 instruction = words.ReadU32LE();
				const uint32 count = instruction >> 16;
				CHECK(count && words.CanRead((count - 1) * 4));
				core::ByteReader operands(words.ReadBytes((count - 1) * 4));
				if ((instruction & 65535) == 17 && count == 2)
				{
					bNativeFeature |= operands.ReadU32LE() == 25;
				}
			}

			CHECK(bNativeFeature == bDynamic && words.GetRemaining() == 0);
		}

		return true;
	}

	bool testStorageWithoutFormat(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(compiler));

		for (const core::uint32 imageFormat : {0u, 3u, 9u})
		{
			const bool bQualified = imageFormat != 0;
			const bool bExtended = imageFormat == 9;
			core::String source(memory);
			CHECK(source.Assign(
				bExtended		 ? "[[vk::image_format(\"r16f\")]] [[vk::binding(0,0)]] RWTexture2D<float> Image;\n"
					: bQualified ? "[[vk::image_format(\"r32f\")]] [[vk::binding(0,0)]] RWTexture2D<float> Image;\n"
								: "[[vk::binding(0,0)]] RWTexture2D<float> Image;\n"));
			CHECK(source.Append(R"(
[[vk::binding(1,0)]] RWStructuredBuffer<float> Result;
[numthreads(1,1,1)] void CSMain()
{
    Image[uint2(0,0)]=Image[uint2(0,0)]+1.0;
    Result[0]=Image[uint2(1,0)];
				})"));
			shader::ShaderCompileRequest request(memory);
			core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot("metadata/storage-format.slang", source.View(), memory);
			CHECK(test::Succeeded(snapshot));
			request.Root = std::move(snapshot).TakeValue();
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign("CSMain"));
			entry.Stage = shader::EShaderStage::Compute;
			CHECK(request.EntryPoints.Add(std::move(entry)));
			shader::ShaderTargetProfile target;
			target.Target = shader::EShaderTarget::SpirV;
			CHECK(request.Targets.Add(target));
			test::MemoryShaderSources resolver;
			shader::ShaderBuildReport report(memory);
			core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, resolver, report, memory);
			if (!built)
			{
				test::PrintBuildReport(report);
			}

			CHECK(test::Succeeded(built));
			core::Result<core::Array<core::Byte>> encoded = built.Value().Artifact.Save(memory);
			CHECK(test::Succeeded(encoded));
			core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
			CHECK(decoded && decoded.Value().GetIdentity() == built.Value().Artifact.GetIdentity());
			const shader::ShaderVariant& variant = decoded.Value().GetVariants()[0];
			const core::uint64 expected = bExtended ? static_cast<core::uint64>(shader::EShaderFeature::StorageImageExtendedFormats)
				: bQualified						? 0
													: static_cast<core::uint64>(shader::EShaderFeature::StorageImageReadWithoutFormat) |
					static_cast<core::uint64>(shader::EShaderFeature::StorageImageWriteWithoutFormat);
			CHECK(variant.Requirements.Features == expected && variant.RequiredCapabilities == 0);
			shader::ShaderEntrySelection selection{decoded.Value(), 0};
			core::Result<ShaderLayout> linked = shader::ShaderLayout::Create({&selection, 1}, memory);
			CHECK(linked && linked.Value().GetFeatureRequirements().Features == expected);
			core::ByteReader words(variant.Bytecode.View());
			CHECK(words.CanRead(20));
			(void)words.ReadBytes(20);
			bool bRead = false;
			bool bWrite = false;
			bool bImage = false;
			bool bNativeExtended = false;

			while (words.CanRead(4))
			{
				const uint32 instruction = words.ReadU32LE();
				const uint32 count = instruction >> 16;
				CHECK(count && words.CanRead((count - 1) * 4));
				core::ByteReader operands(words.ReadBytes((count - 1) * 4));
				if ((instruction & 65535) == 17 && count == 2)
				{
					const uint32 capability = operands.ReadU32LE();
					CHECK(capability != 25);
					bNativeExtended |= capability == 49;
					bRead |= capability == 55;
					bWrite |= capability == 56;
				}

				if ((instruction & 65535) == 25 && count >= 9)
				{
					(void)operands.ReadBytes(6 * 4);
					CHECK(operands.ReadU32LE() == 2);
					CHECK(operands.ReadU32LE() == imageFormat);
					bImage = true;
				}
			}

			CHECK(bImage && bRead == !bQualified && bWrite == !bQualified && words.GetRemaining() == 0);
			CHECK(bNativeExtended == bExtended);
		}

		return true;
	}

	bool testFragmentQuad(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(compiler));

		for (core::uint32 mode = 0; mode < 3; ++mode)
		{
			const core::StringView source = mode == 0 ? "[shader(\"fragment\")] float4 PSMain(float4 p:SV_Position):SV_Target0 { return "
														"float4(p.xy,0,1); }"
				: mode == 1							  ? "[shader(\"fragment\")] float4 PSMain(float4 p:SV_Position):SV_Target0 { return "
														"float4(QuadReadAcrossX(p.x),QuadReadAcrossY(p.y),QuadReadAcrossDiagonal(p.x),1); }"
													: "[shader(\"fragment\")] float4 PSMain(float4 p:SV_Position):SV_Target0 { return "
														"float4(WaveActiveSum(p.x)); }";
			shader::ShaderCompileRequest request(memory);
			core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot("metadata/fragment-quad.slang", source, memory);
			CHECK(test::Succeeded(snapshot));
			request.Root = core::Move(snapshot).TakeValue();
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign("PSMain"));
			entry.Stage = shader::EShaderStage::Pixel;
			CHECK(request.EntryPoints.Add(core::Move(entry)));
			shader::ShaderTargetProfile target;
			target.Target = shader::EShaderTarget::SpirV;
			CHECK(request.Targets.Add(target));
			test::MemoryShaderSources sources;
			shader::ShaderBuildReport report(memory);
			core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, sources, report, memory);
			if (mode == 2)
			{
				CHECK(!built && built.GetError().Code == core::EErrorCode::Unsupported);
				continue;
			}

			if (!built)
			{
				test::PrintBuildReport(report);
			}

			CHECK(test::Succeeded(built));
			const core::uint64 expected = mode == 1 ? static_cast<core::uint64>(shader::EShaderFeature::Subgroup) | static_cast<core::uint64>(shader::EShaderFeature::SubgroupQuad)
													: 0;
			core::Result<core::Array<core::Byte>> encoded = built.Value().Artifact.Save(memory);
			CHECK(test::Succeeded(encoded));
			core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
			CHECK(decoded && decoded.Value().GetIdentity() == built.Value().Artifact.GetIdentity());
			const shader::ShaderVariant& variant = decoded.Value().GetVariants()[0];
			CHECK(variant.Requirements.Features == expected && variant.RequiredCapabilities == 0);
			const shader::ShaderEntrySelection selection{decoded.Value(), 0};
			core::Result<ShaderLayout> linked = shader::ShaderLayout::Create({&selection, 1}, memory);
			CHECK(linked && linked.Value().GetFeatureRequirements().Features == expected);
			core::ByteReader words(variant.Bytecode.View());
			CHECK(words.CanRead(20));
			(void)words.ReadBytes(20);
			core::uint32 operations = 0;

			while (words.CanRead(4))
			{
				const uint32 instruction = words.ReadU32LE();
				const uint32 count = instruction >> 16;
				CHECK(count && words.CanRead((count - 1) * 4));
				core::ByteReader operands(words.ReadBytes((count - 1) * 4));
				if ((instruction & 65535) == 17 && count == 2)
				{
					const uint32 capability = operands.ReadU32LE();
					if (capability >= 61 && capability <= 68)
					{
						operations |= 1u << (capability - 61);
					}
				}
			}

			// Capability68 implies61; this pinned compiler declares only68 for the fragment control.
			CHECK(operations == (mode == 1 ? 128u : 0u) && words.GetRemaining() == 0);
		}

		return true;
	}

	bool testUnrepresentedStorageCapability(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot(
			"metadata/unrepresented-storage.slang", R"slang(
[[vk::binding(0,0)]] RWStructuredBuffer<uint16_t> Output;
[numthreads(1,1,1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    Output[id.x] = uint16_t(id.x + 1);
			})slang",
			memory);
		CHECK(test::Succeeded(source));
		request.Root = std::move(source).TakeValue();
		shader::ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign("CSMain"));
		entry.Stage = shader::EShaderStage::Compute;
		CHECK(request.EntryPoints.Add(std::move(entry)));
		shader::ShaderTargetProfile target;
		target.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(target));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> rejected = compiler.Value()->Compile(request, resolver, report, memory);
		CHECK(!rejected && rejected.GetError().Code == core::EErrorCode::Unsupported);
		CHECK(rejected.GetError().Message.View().Contains("SPIR-V requires a capability that this engine shader ABI cannot express: "));

		return true;
	}

	bool testCrossCompilerScalarBufferLayouts(core::MemoryResource& memory)
	{
		const shader::EShaderCompiler compilers[]{shader::EShaderCompiler::Slang, shader::EShaderCompiler::Dxc};
		core::Hash256 identities[2][3];

		for (core::uint32 compilerIndex = 0; compilerIndex < 2; ++compilerIndex)
		{
			core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(compilers[compilerIndex], memory);
			CHECK(test::Succeeded(compiler));
			if (compilers[compilerIndex] == shader::EShaderCompiler::Dxc)
			{
				CHECK(compiler.Value()->GetToolchainIdentity().AdapterVersion >= 2);
			}

			shader::ShaderCompileRequest request(memory);
			request.Language = shader::EShaderSourceLanguage::Hlsl;
			core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot(
				"metadata/scalar-buffer-layout.hlsl", R"hlsl(
StructuredBuffer<uint> UIntInput : register(t0);
RWStructuredBuffer<uint> UIntOutput : register(u1);
StructuredBuffer<int> IntInput : register(t2);
RWStructuredBuffer<int> IntOutput : register(u3);
StructuredBuffer<float> FloatInput : register(t4);
RWStructuredBuffer<float> FloatOutput : register(u5);
cbuffer Parameters : register(b6) { uint Element; uint element; };
[numthreads(1,1,1)] void CSMain()
{
    UIntOutput[0] = UIntInput[0] + Element + element;
    IntOutput[0] = IntInput[0] + 2;
    FloatOutput[0] = FloatInput[0] * 0.5;
				})hlsl",
				memory);
			CHECK(test::Succeeded(source));
			request.Root = std::move(source).TakeValue();
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign("CSMain"));
			entry.Stage = shader::EShaderStage::Compute;
			CHECK(request.EntryPoints.Add(std::move(entry)));
			shader::ShaderTargetProfile target;
			target.Target = shader::EShaderTarget::Dxil;
			CHECK(request.Targets.Add(target));
			test::MemoryShaderSources resolver;
			shader::ShaderBuildReport report(memory);
			core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, resolver, report, memory);
			if (!built)
			{
				test::PrintBuildReport(report);
			}

			CHECK(test::Succeeded(built));
			const shader::ShaderVariant& variant = built.Value().Artifact.GetVariants()[0];
			core::uint32 counts[3]{};
			const shader::EShaderScalar scalars[]{shader::EShaderScalar::UnsignedInteger, shader::EShaderScalar::SignedInteger, shader::EShaderScalar::Float};
			bool bUppercase = false;
			bool bLowercase = false;

			for (const shader::ShaderBinding& binding : variant.Bindings)
			{
				CHECK(binding.DataLayout < variant.Layouts.Size());
				const shader::ShaderDataLayout& layout = variant.Layouts[binding.DataLayout];
				const StringView name = binding.Name.View();
				if (name == "Parameters")
				{
					for (const shader::ShaderDataField& field : layout.Fields)
					{
						bUppercase |= field.Name.View() == "Element";
						bLowercase |= field.Name.View() == "element";
					}

					continue;
				}

				const core::uint32 index = name == "UIntInput" || name == "UIntOutput" ? 0u : name == "IntInput" || name == "IntOutput" ? 1u : 2u;
				CHECK(layout.ByteSize == 4 && layout.Alignment == 4 && layout.ElementStride == 4 && layout.Fields.Size() == 1);
				const shader::ShaderDataField& field = layout.Fields[0];
				CHECK(
					field.Name.View() == "$element" && field.ByteOffset == 0 && field.ByteSize == 4 && field.Scalar == scalars[index] && field.ScalarWidth == 32 &&
					field.VectorWidth == 1 && field.ArrayCount == 1 && field.ArrayStride == 0 && field.MatrixStride == 0 && field.NestedLayout == shader::NO_DATA_LAYOUT);
				if (counts[index] == 0)
				{
					identities[compilerIndex][index] = layout.Identity;
				}

				CHECK(layout.Identity == identities[compilerIndex][index]);
				++counts[index];
			}

			CHECK(counts[0] == 2 && counts[1] == 2 && counts[2] == 2 && bUppercase && bLowercase);
			CHECK(
				identities[compilerIndex][0] != identities[compilerIndex][1] && identities[compilerIndex][0] != identities[compilerIndex][2] &&
				identities[compilerIndex][1] != identities[compilerIndex][2]);
			core::Result<core::Array<core::Byte>> encoded = built.Value().Artifact.Save(memory);
			CHECK(test::Succeeded(encoded));
			core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
			CHECK(decoded && decoded.Value().GetIdentity() == built.Value().Artifact.GetIdentity());
		}

		for (core::uint32 index = 0; index < 3; ++index)
		{
			CHECK(identities[0][index] == identities[1][index]);
		}

		return true;
	}

	bool testMinimumPrecision(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Dxc, {}, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot(
			"metadata/minimum-precision.hlsl", R"hlsl(
Texture2D<float> Input : register(t0);
RWTexture2D<float> Output : register(u1);
[numthreads(1,1,1)] void Minimum()
{
    min16float value=(min16float)Input.Load(int3(0,0,0));
    Output[uint2(0,0)]=float(value*min16float(0.5)+min16float(0.125));
}
[numthreads(1,1,1)] void Full()
{
    float value=Input.Load(int3(0,0,0));
    Output[uint2(0,0)]=value*0.5+0.125;
}
			)hlsl",
			memory);
		CHECK(test::Succeeded(source));
		request.Root = std::move(source).TakeValue();

		for (core::StringView name : {"Minimum", "Full"})
		{
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign(name));
			entry.Stage = shader::EShaderStage::Compute;
			CHECK(request.EntryPoints.Add(std::move(entry)));
		}

		shader::ShaderTargetProfile target;
		target.Target = shader::EShaderTarget::Dxil;
		CHECK(request.Targets.Add(target));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, resolver, report, memory);
		if (!built)
			test::PrintBuildReport(report);
		CHECK(test::Succeeded(built));
		core::Result<core::Array<core::Byte>> encoded = built.Value().Artifact.Save(memory);
		CHECK(test::Succeeded(encoded));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == built.Value().Artifact.GetIdentity());

		for (core::StringView name : {"Minimum", "Full"})
		{
			core::Result<core::uint32> selected = decoded.Value().FindVariant({name, target});
			CHECK(test::Succeeded(selected));
			const shader::ShaderVariant& variant = decoded.Value().GetVariants()[selected.Value()];
			const bool bMinimum = name == "Minimum";
			const core::uint64 expected = bMinimum ? static_cast<core::uint64>(shader::EShaderFeature::MinimumPrecision) : 0;
			CHECK(variant.Requirements.Features == expected && variant.RequiredCapabilities == 0);
			shader::ShaderEntrySelection selection{decoded.Value(), selected.Value()};
			core::Result<ShaderLayout> linked = shader::ShaderLayout::Create({&selection, 1}, memory);
			CHECK(linked && linked.Value().GetFeatureRequirements().Features == expected);
			const Span<const core::Byte> code = variant.Bytecode.View();
			CHECK(code.Size() >= 32);
			core::uint32 size = 0, count = 0;
			std::memcpy(&size, code.Data() + 24, 4);
			std::memcpy(&count, code.Data() + 28, 4);
			CHECK(size == code.Size() && count <= (code.Size() - 32) / 4);
			bool bFlagsFound = false;

			for (core::uint32 part = 0; part < count; ++part)
			{
				core::uint32 offset = 0, tag = 0, length = 0;
				std::memcpy(&offset, code.Data() + 32 + part * 4, 4);
				CHECK(offset <= code.Size() - 8);
				std::memcpy(&tag, code.Data() + offset, 4);
				std::memcpy(&length, code.Data() + offset + 4, 4);
				CHECK(length <= code.Size() - offset - 8);
				if (tag == 0x30494653u)
				{
					CHECK(!bFlagsFound && length == 8);
					core::uint64 flags = 0;
					std::memcpy(&flags, code.Data() + offset + 8, 8);
					CHECK(flags == (bMinimum ? 0x10u : 0u));
					bFlagsFound = true;
				}
			}

			CHECK(bFlagsFound || !bMinimum);
		}

		return true;
	}

	core::uint32 entryWord(core::Span<const core::Byte> code, core::usize offset)
	{
		core::uint32 value;
		std::memcpy(&value, code.Data() + offset * 4, 4);

		return value;
	}

	bool nativeEntryInventory(const shader::ShaderVariant& variant)
	{
		const Span<const core::Byte> code = variant.Bytecode.View();
		CHECK(code.Size() >= 20 && code.Size() % 4 == 0 && entryWord(code, 0) == 0x07230203);
		const core::uint32 bound = entryWord(code, 3);
		CHECK(bound > 0 && bound <= 262144);

		struct Native final
		{
			core::uint32 Set = ~core::uint32{0};
			core::uint32 Binding = ~core::uint32{0};
			core::uint32 Definition = 0;
		};

		std::vector<Native> nodes(bound);

		for (core::usize offset = 5; offset < code.Size() / 4;)
		{
			const core::uint32 word = entryWord(code, offset);
			const core::uint32 size = word >> 16;
			const core::uint32 op = word & 65535;
			CHECK(size > 0 && size <= code.Size() / 4 - offset);
			if ((op >= 19 && op <= 33) || op == 43 || op == 59)
			{
				const core::uint32 id = entryWord(code, offset + (op == 43 || op == 59 ? 2 : 1));
				CHECK(id < bound);
				nodes[id].Definition = static_cast<core::uint32>(offset);
			}

			if (op == 71 && size == 4)
			{
				const core::uint32 id = entryWord(code, offset + 1);
				CHECK(id < bound);
				if (entryWord(code, offset + 2) == 33)
				{
					nodes[id].Binding = entryWord(code, offset + 3);
				}

				if (entryWord(code, offset + 2) == 34)
				{
					nodes[id].Set = entryWord(code, offset + 3);
				}
			}

			offset += size;
		}

		struct Location final
		{
			core::uint32 Set, Binding;
		};

		std::vector<Location> locations;

		for (const Native& node : nodes)
		{
			if (node.Set == ~core::uint32{0} && node.Binding == ~core::uint32{0})
			{
				continue;
			}

			CHECK(node.Set != ~core::uint32{0} && node.Binding != ~core::uint32{0} && node.Definition != 0);
			CHECK((entryWord(code, node.Definition) & 65535) == 59);
			const core::uint32 pointer = entryWord(code, node.Definition + 1);
			CHECK(pointer < bound && nodes[pointer].Definition != 0);
			const core::uint32 type = entryWord(code, nodes[pointer].Definition + 3);
			CHECK(type < bound && nodes[type].Definition != 0);
			const core::uint32 nativeType = nodes[type].Definition;
			const core::uint32 op = entryWord(code, nativeType) & 65535;
			const bool runtime = op == 29;
			core::uint32 count = 1;
			if (op == 28)
			{
				const core::uint32 length = entryWord(code, nativeType + 3);
				CHECK(length < bound && nodes[length].Definition != 0);
				CHECK((entryWord(code, nodes[length].Definition) & 65535) == 43);
				count = entryWord(code, nodes[length].Definition + 3);
			}

			core::uint32 matches = 0;

			for (const shader::ShaderBinding& binding : variant.Bindings)
			{
				if (binding.Space == node.Set && binding.Binding == node.Binding)
				{
					CHECK(binding.Scope == shader::EShaderBindingScope::Global);
					CHECK(binding.CountMode == (runtime ? shader::EShaderDescriptorCountMode::Runtime : shader::EShaderDescriptorCountMode::Fixed));
					CHECK(binding.ArrayCount == (runtime ? 0u : count));
					++matches;
				}
			}

			CHECK(matches == 1);
			bool seen = false;

			for (const Location& location : locations)
			{
				seen |= location.Set == node.Set && location.Binding == node.Binding;
			}

			if (!seen)
			{
				locations.push_back({node.Set, node.Binding});
			}
		}

		const core::usize nativeCount = locations.size();
		CHECK(variant.Bindings.Size() == nativeCount && variant.ResourceUses.Size() == nativeCount);
		std::vector<bool> uses(nativeCount, false);

		for (const shader::ShaderResourceUse& use : variant.ResourceUses)
		{
			CHECK(use.BindingIndex < nativeCount && !uses[use.BindingIndex]);
			CHECK(use.Access == variant.Bindings[use.BindingIndex].Access);
			uses[use.BindingIndex] = true;
		}

		return true;
	}

	struct ResourceEntry final
	{
		const char* Name;
		shader::EShaderStage Stage;
		core::uint32 Count;
	};

	bool compileEntryResources(core::StringView source, core::Span<const ResourceEntry> entries, core::MemoryResource& memory, shader::Shader& output)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(compiler));
		CHECK(compiler.Value()->GetToolchainIdentity().AdapterVersion == 2);
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> root = shader::CreateShaderSourceSnapshot("resources/entries.slang", source, memory);
		CHECK(test::Succeeded(root));
		request.Root = core::Move(root).TakeValue();

		for (const ResourceEntry& value : entries)
		{
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign(value.Name));
			entry.Stage = value.Stage;
			CHECK(request.EntryPoints.Add(core::Move(entry)));
		}

		for (const char* name : {"ROXELL_STRATA_MANAGED_BUFFERS", "ROXELL_STRATA_TERRAIN_COVERAGE"})
		{
			shader::ShaderDefine define(memory);
			CHECK(define.Name.Assign(name) && define.Value.Assign("1"));
			CHECK(request.Defines.Add(core::Move(define)));
		}

		shader::ShaderTargetProfile target;
		target.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(target));
		test::MemoryShaderSources sources;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, sources, report, memory);
		if (!built)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(built));
		core::Result<core::Array<core::Byte>> bytes = built.Value().Artifact.Save(memory);
		CHECK(test::Succeeded(bytes));
		core::Result<Shader> decoded = shader::Shader::Load(bytes.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == built.Value().Artifact.GetIdentity());
		output = core::Move(decoded).TakeValue();
		CHECK(output.GetVariants().Size() == entries.Size());

		for (core::uint32 index = 0; index < output.GetVariants().Size(); ++index)
		{
			const shader::ShaderVariant& variant = output.GetVariants()[index];
			const shader::ShaderEntryPoint& entry = output.GetEntryPoints()[variant.EntryPointIndex];
			CHECK(nativeEntryInventory(variant));
			bool found = false;

			for (const ResourceEntry& value : entries)
			{
				if (entry.Name != value.Name)
				{
					continue;
				}

				CHECK(entry.Stage == value.Stage && variant.Bindings.Size() == value.Count);
				found = true;
			}

			CHECK(found);
			const shader::ShaderEntrySelection selection{output, index};
			core::Result<ShaderLayout> linked = shader::ShaderLayout::Create({&selection, 1}, memory);
			CHECK(test::Succeeded(linked) && linked.Value().GetBindings().Size() == variant.Bindings.Size());
		}

		return true;
	}

	bool testEntryResources(core::MemoryResource& memory)
	{
		constexpr char SOURCE[] = R"slang(
struct P { uint4 Value; };
[[vk::binding(0,0)]] ConstantBuffer<P> Parameters;
[[vk::binding(1,0)]] StructuredBuffer<uint> SharedInput;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> LeftOutput;
[[vk::binding(3,0)]] RWStructuredBuffer<uint> RightOutput;
[[vk::binding(4,0)]] Texture2D<float4> UnusedTexture;
[[vk::binding(5,0)]] SamplerState UnusedSampler;
[[vk::binding(6,0)]] Texture2D<float4> Images[2];
[[vk::binding(7,0)]] SamplerState ImageSampler;
[[vk::binding(8,0)]] RWTexture2D<uint> ImageOutput;
[numthreads(1,1,1)] void Left() {
    LeftOutput[0] = SharedInput[0] + Parameters.Value.y +
        uint(Images[Parameters.Value.x & 1].SampleLevel(ImageSampler, float2(0.5), 0).x);
}
[numthreads(1,1,1)] void Right() { RightOutput[0] = SharedInput[0] + 1; ImageOutput[uint2(0)] = SharedInput[0] + 3; }
float4 Vertex(float3 position : POSITION) : SV_Position { return float4(position,1); }
			)slang";
		const ResourceEntry entries[]{{"Left", shader::EShaderStage::Compute, 5}, {"Right", shader::EShaderStage::Compute, 3}, {"Vertex", shader::EShaderStage::Vertex, 0}};
		shader::Shader artifact;
		CHECK(compileEntryResources(SOURCE, entries, memory, artifact));

		for (const shader::ShaderVariant& variant : artifact.GetVariants())
		{
			for (const shader::ShaderBinding& binding : variant.Bindings)
			{
				CHECK(binding.Name != "UnusedTexture" && binding.Name != "UnusedSampler");
			}
		}

		return true;
	}

	bool testActualEntryResources(const char* root, core::MemoryResource& memory)
	{
		const auto run = [&](const char* file, core::Span<const ResourceEntry> entries) -> bool
		{
			const std::filesystem::path path = std::filesystem::path(root) / "Engine/Tools/ShaderCompiler/Tests/Data/Reflection" / file;
			std::ifstream stream(path, std::ios::binary);
			CHECK(stream.good());
			const std::string source(std::istreambuf_iterator<char>(stream), {});
			shader::Shader artifact;
			CHECK(compileEntryResources({source.data(), source.size()}, entries, memory, artifact));
			if (std::strcmp(file, "DomeIrradiance.slang") == 0)
			{
				for (const shader::ShaderVariant& variant : artifact.GetVariants())
				{
					bool retained = false;

					for (const shader::ShaderBinding& binding : variant.Bindings)
					{
						retained |= binding.Name == "PreviousSkyLight";
					}

					CHECK(retained);
				}
			}

			return true;
		};

		const ResourceEntry ao[]{{"VSMain", shader::EShaderStage::Vertex, 0}, {"PSDepthBounds", shader::EShaderStage::Pixel, 3}, {"PSHalfDepth", shader::EShaderStage::Pixel, 3},
			{"CSNormalMotion", shader::EShaderStage::Compute, 6}, {"CSReverseDepth", shader::EShaderStage::Compute, 2}};
		const ResourceEntry post[]{{"CopyColor", shader::EShaderStage::Compute, 3}, {"PrepareExposure", shader::EShaderStage::Compute, 6},
			{"PrepareComposedExposure", shader::EShaderStage::Compute, 5}, {"PrepareLuminance", shader::EShaderStage::Compute, 6}};
		const ResourceEntry compose[]{{"ComposeHdr", shader::EShaderStage::Compute, 4}, {"LinearizeDepth", shader::EShaderStage::Compute, 3}};
		const ResourceEntry world[]{{"CSMain", shader::EShaderStage::Compute, 8}};
		const ResourceEntry irradiance[]{{"InitializeMain", shader::EShaderStage::Compute, 6}, {"UpdateMain", shader::EShaderStage::Compute, 6}};
		CHECK(run("AmbientOcclusionPrepare.slang", ao));
		CHECK(run("StrataPostPrepare.slang", post));
		CHECK(run("StrataSceneCompose.slang", compose));
		CHECK(run("ReflectionWorldHeight.slang", world));
		CHECK(run("DomeIrradiance.slang", irradiance));

		return true;
	}

} // namespace

TEST(ShaderCompilerMetadata, DemoteCompilerProfiles)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDemoteCompilerProfiles(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, Int64AtomicRequirements)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testInt64AtomicRequirements(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, SampleIndexCompiler)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testSampleIndexCompiler(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, SampledStencilFormat)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testSampledStencilFormat(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, ImageGatherExtended)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testImageGatherExtended(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, StorageWithoutFormat)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testStorageWithoutFormat(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, FragmentQuad)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testFragmentQuad(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, UnrepresentedStorageCapability)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testUnrepresentedStorageCapability(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, CrossCompilerScalarBufferLayouts)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testCrossCompilerScalarBufferLayouts(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, MinimumPrecision)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testMinimumPrecision(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, EntryResources)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testEntryResources(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, CompilerSlangSpirV)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testCompiler(shader::EShaderCompiler::Slang, shader::EShaderTarget::SpirV, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, CompilerSlangDxil)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testCompiler(shader::EShaderCompiler::Slang, shader::EShaderTarget::Dxil, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, CompilerDxcDxil)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testCompiler(shader::EShaderCompiler::Dxc, shader::EShaderTarget::Dxil, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, DepthOnlyCompilerSlangSpirV)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDepthOnlyCompiler(shader::EShaderCompiler::Slang, shader::EShaderTarget::SpirV, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, DepthOnlyCompilerSlangDxil)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDepthOnlyCompiler(shader::EShaderCompiler::Slang, shader::EShaderTarget::Dxil, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, DepthOnlyCompilerDxcDxil)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDepthOnlyCompiler(shader::EShaderCompiler::Dxc, shader::EShaderTarget::Dxil, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, TypedUavLoadSlang)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testTypedUavLoadRequirements(shader::EShaderCompiler::Slang, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, TypedUavLoadDxc)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testTypedUavLoadRequirements(shader::EShaderCompiler::Dxc, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderCompilerMetadata, RendererSourceFiles)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
	EXPECT_TRUE(testActualEntryResources(SHADER_COMPILER_REPOSITORY, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
