#include "ShaderTestSources.h"
#include "../Private/Dxc/DxcSpirvReflector.h"

#include <ByteOperations.hpp>
#include <Cancellation.h>
#include <FailingMemoryResource.h>
#include <ShaderBuilder.h>
#include <ShaderLayout.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <limits>
#include <vector>

#include "ShaderCompilerTest.h"

namespace
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	constexpr char WITNESS[] = R"hlsl(struct ParametersData { float4 Rows[2]; uint Factor; float3 Offset; };
struct Record { float4 Value; uint Index; };
[[vk::binding(0,0)]] ConstantBuffer<ParametersData> Parameters : register(b0);
[[vk::binding(1,0)]] StructuredBuffer<Record> Input : register(t1);
[[vk::binding(2,0)]] RWStructuredBuffer<float4> Output : register(u2);
[[vk::binding(3,0)]] Texture2D<float4> TextureInput : register(t3);
[[vk::binding(4,0)]] SamplerState PointSampler : register(s4);
[[vk::binding(5,0)]] RWTexture2D<float4> ImageOutput : register(u5);
[numthreads(2,2,1)] void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x + id.y * 2;
    float4 sampled = TextureInput.SampleLevel(PointSampler, (float2(id.xy)+0.5)/2.0, 0);
    float4 value = Input[index].Value * Parameters.Rows[0] + Parameters.Rows[1];
    value += sampled + float4(Parameters.Offset, Parameters.Factor) + float(Input[index].Index);
    Output[index] = value;
    ImageOutput[id.xy] = value;
}
		)hlsl";

	const shader::ShaderBinding* binding(const shader::ShaderVariant& variant, core::StringView name)
	{
		for (const shader::ShaderBinding& value : variant.Bindings)
		{
			if (value.Name == name)
			{
				return &value;
			}
		}

		return nullptr;
	}

	const shader::ShaderDataField* field(const shader::ShaderDataLayout& layout, core::StringView name)
	{
		for (const shader::ShaderDataField& value : layout.Fields)
		{
			if (value.Name == name)
			{
				return &value;
			}
		}

		return nullptr;
	}

	bool compile(
		shader::IShaderCompiler& compiler,
		core::StringView text,
		core::MemoryResource& memory,
		shader::Shader& output,
		bool bBoth = false,
		bool bManaged = false,
		bool bReject = false)
	{
		shader::ShaderCompileRequest request(memory);
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot("dxc/native-layout.hlsl", text, memory);
		CHECK(test::Succeeded(source));
		request.Root = core::Move(source).TakeValue();
		shader::ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign("CSMain"));
		entry.Stage = shader::EShaderStage::Compute;
		CHECK(request.EntryPoints.Add(core::Move(entry)));
		if (bManaged)
		{
			shader::ShaderDefine define(memory);
			CHECK(define.Name.Assign("ROXELL_STRATA_MANAGED_BUFFERS") && define.Value.Assign("1"));
			CHECK(request.Defines.Add(core::Move(define)));
		}

		shader::ShaderTargetProfile target;
		if (bBoth)
		{
			CHECK(request.Targets.Add(target));
		}

		target.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(target));
		test::MemoryShaderSources sources;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> result = compiler.Compile(request, sources, report, memory);
		if (bReject)
		{
			CHECK(!result && result.GetError().Code == core::EErrorCode::Unsupported);

			return true;
		}

		if (!result)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(result));
		core::Result<core::Array<core::Byte>> encoded = result.Value().Artifact.Save(memory);
		CHECK(test::Succeeded(encoded));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == result.Value().Artifact.GetIdentity());
		output = core::Move(decoded).TakeValue();

		return true;
	}

	bool profileRejections(shader::IShaderCompiler& compiler, core::MemoryResource& memory)
	{
		for (core::uint32 mode = 0; mode < 4; ++mode)
		{
			shader::ShaderCompileRequest request(memory);
			request.Language = shader::EShaderSourceLanguage::Hlsl;
			core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot("dxc/profile-gate.hlsl", WITNESS, memory);
			CHECK(test::Succeeded(source));
			request.Root = core::Move(source).TakeValue();
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign("CSMain"));
			entry.Stage = mode == 0 ? shader::EShaderStage::Vertex : mode == 1 ? shader::EShaderStage::Pixel : shader::EShaderStage::Compute;
			CHECK(request.EntryPoints.Add(core::Move(entry)));
			if (mode == 2)
			{
				request.DebugInfo = shader::EShaderDebugInfo::Standard;
			}

			if (mode == 3)
			{
				shader::ShaderEntryPoint vertex(memory);
				CHECK(vertex.Name.Assign("VSMain"));
				vertex.Stage = shader::EShaderStage::Vertex;
				CHECK(request.EntryPoints.Add(core::Move(vertex)));
			}

			shader::ShaderTargetProfile target;
			target.Target = shader::EShaderTarget::SpirV;
			CHECK(request.Targets.Add(target));
			test::MemoryShaderSources sources;
			shader::ShaderBuildReport report(memory);
			core::Result<ShaderBuildPlan> rejected = compiler.PrepareBuild(request, sources, report, memory);
			CHECK(!rejected && rejected.GetError().Code == core::EErrorCode::Unsupported);
			CHECK(sources.Calls == 0);
		}

		return true;
	}

	bool layouts(const shader::Shader& artifact, core::MemoryResource& memory)
	{
		CHECK(artifact.GetVariants().Size() == 2);
		core::Hash256 identities[2];

		for (const shader::ShaderVariant& variant : artifact.GetVariants())
		{
			const bool bSpirV = variant.Target.Target == shader::EShaderTarget::SpirV;
			const shader::ShaderBinding* parameters = binding(variant, "Parameters");
			const shader::ShaderBinding* input = binding(variant, "Input");
			const shader::ShaderBinding* output = binding(variant, "Output");
			CHECK(parameters && input && output && variant.Bindings.Size() == 6);
			CHECK(parameters->Kind == shader::EShaderBindingKind::ConstantBuffer && input->Kind == shader::EShaderBindingKind::ReadOnlyBuffer);
			CHECK(output->Kind == shader::EShaderBindingKind::ReadWriteBuffer);
			const shader::ShaderDataLayout& constants = variant.Layouts[parameters->DataLayout];
			const shader::ShaderDataLayout* members = &constants;
			if (!bSpirV)
			{
				CHECK(constants.Fields.Size() == 1);
				const shader::ShaderDataField& wrapper = constants.Fields[0];
				CHECK(wrapper.Name == "Parameters" && wrapper.ByteOffset == 0 && wrapper.ByteSize == 48 && wrapper.NestedLayout < variant.Layouts.Size());
				members = &variant.Layouts[wrapper.NestedLayout];
				CHECK(members->ByteSize == 48);
			}

			const shader::ShaderDataField* rows = field(*members, "Rows");
			const shader::ShaderDataField* factor = field(*members, "Factor");
			const shader::ShaderDataField* offset = field(*members, "Offset");
			if (!rows || !factor || !offset)
			{
				for (const shader::ShaderDataField& member : constants.Fields)
				{
					std::fprintf(
						stderr, "DXC %s field %.*s offset=%llu nested=%u\n", bSpirV ? "SPIR-V" : "DXIL", static_cast<int>(member.Name.Size()), member.Name.Data(),
						static_cast<unsigned long long>(member.ByteOffset), member.NestedLayout);
				}
			}

			CHECK(rows && factor && offset);
			std::fprintf(
				stderr, "DXC %s layout: CB=%llu, Offset=%llu, InputStride=%llu.\n", bSpirV ? "SPIR-V" : "DXIL", static_cast<unsigned long long>(constants.ByteSize),
				static_cast<unsigned long long>(offset->ByteOffset), static_cast<unsigned long long>(variant.Layouts[input->DataLayout].ElementStride));
			CHECK(rows->ByteOffset == 0 && rows->ArrayCount == 2 && rows->ArrayStride == 16);
			CHECK(factor->ByteOffset == 32 && factor->Scalar == shader::EShaderScalar::UnsignedInteger && factor->ScalarWidth == 32);
			CHECK(offset->ByteOffset == (bSpirV ? 48u : 36u) && offset->VectorWidth == 3);
			CHECK(constants.ByteSize == (bSpirV ? 64u : 48u));
			CHECK(variant.Layouts[input->DataLayout].ElementStride == (bSpirV ? 32u : 20u));
			CHECK(variant.Layouts[output->DataLayout].ElementStride == 16);
			identities[bSpirV ? 1 : 0] = constants.Identity;
			CHECK(variant.Requirements.Features == 0 && variant.RequiredCapabilities == 0);
			CHECK(variant.Requirements.MinimumWorkgroupInvocations == 4);
			const core::uint32 index = static_cast<core::uint32>(&variant - artifact.GetVariants().Data());
			const shader::ShaderEntrySelection selected{artifact, index};
			core::Result<ShaderLayout> linked = shader::ShaderLayout::Create({&selected, 1}, memory);
			CHECK(test::Succeeded(linked));
			if (bSpirV)
			{
				for (const shader::ShaderBinding& value : variant.Bindings)
				{
					CHECK(value.RegisterNamespace == shader::EShaderRegisterClass::Unified);
				}
			}
		}

		CHECK(identities[0] != identities[1]);

		return true;
	}

	core::uint32 word(const std::vector<core::Byte>& bytes, core::usize offset)
	{
		core::uint32 value;
		std::memcpy(&value, bytes.data() + offset * 4, 4);

		return value;
	}

	void set(std::vector<core::Byte>& bytes, core::usize offset, core::uint32 value)
	{
		std::memcpy(bytes.data() + offset * 4, &value, 4);
	}

	bool initialize(shader::ShaderVariant& variant, shader::ShaderEntryPoint& entry, core::Span<const core::Byte> code)
	{
		variant.Target.Target = shader::EShaderTarget::SpirV;
		entry.Stage = shader::EShaderStage::Compute;
		CHECK(entry.Name.Assign("CSMain") && variant.EmittedEntryPoint.Assign("CSMain"));
		CHECK(variant.Bytecode.ResizeUninitialized(code.Size()));
		core::CopyBytes(variant.Bytecode.Data(), code.Data(), code.Size());

		return true;
	}

	bool malformed(core::Span<const core::Byte> code, core::MemoryResource& memory)
	{
		for (core::uint32 mode = 0; mode < 9; ++mode)
		{
			std::vector<core::Byte> changed(code.Data(), code.Data() + code.Size());
			bool bChanged = true;
			if (mode == 0)
			{
				set(changed, 0, 0);
			}
			else if (mode == 1)
			{
				set(changed, 5, 17);
			}
			else if (mode == 2)
			{
				changed.pop_back();
			}
			else if (mode == 3)
			{
				set(changed, 3, 262145);
			}
			else
			{
				bChanged = false;

				for (core::usize offset = 5; offset < changed.size() / 4; offset += word(changed, offset) >> 16)
				{
					const core::uint32 instruction = word(changed, offset);
					const core::uint32 op = instruction & 65535;
					if (mode == 4 && op == 17)
					{
						set(changed, offset + 1, 22);
						bChanged = true;
						break;
					}

					if (mode == 5 && op == 71 && word(changed, offset + 2) == 33 && word(changed, offset + 3) != 0)
					{
						set(changed, offset + 3, 0);
						bChanged = true;
						break;
					}

					if (mode == 6 && op == 72 && word(changed, offset + 3) == 35 && word(changed, offset + 4) > 0)
					{
						set(changed, offset + 4, 0);
						bChanged = true;
						break;
					}

					if (mode == 7 && op == 71 && word(changed, offset + 2) == 6)
					{
						set(changed, offset + 3, 0);
						bChanged = true;
						break;
					}

					if (mode == 8 && op == 30 && (instruction >> 16) > 3)
					{
						set(changed, offset + 2, word(changed, offset + 1));
						bChanged = true;
						break;
					}
				}
			}

			CHECK(bChanged);
			shader::ShaderVariant variant(memory);
			shader::ShaderEntryPoint entry(memory);
			CHECK(initialize(variant, entry, {changed.data(), changed.size()}));
			core::Result<void> result = shader::reflectDxcSpirv(variant, entry, memory);
			CHECK(!result && result.GetError().Code == core::EErrorCode::Unsupported);
		}

		{
			shader::ShaderVariant variant(memory);
			shader::ShaderEntryPoint entry(memory);
			CHECK(initialize(variant, entry, code));
			core::CancellationSource cancelled(memory);
			cancelled.RequestCancellation();
			core::Result<void> result = shader::reflectDxcSpirv(variant, entry, memory, cancelled.GetToken());
			CHECK(!result && result.GetError().Code == core::EErrorCode::Cancelled && variant.Bindings.IsEmpty());
		}

		bool bSucceeded = false;

		for (core::usize budget = 0; budget < 256 && !bSucceeded; ++budget)
		{
			core::FailingMemoryResource scratch(memory, std::numeric_limits<core::usize>::max());
			shader::ShaderVariant variant(scratch);
			shader::ShaderEntryPoint entry(scratch);
			CHECK(initialize(variant, entry, code));
			scratch.Reset(budget);
			core::Result<void> result = shader::reflectDxcSpirv(variant, entry, scratch);
			bSucceeded = static_cast<bool>(result);
			if (!result)
			{
				CHECK(result.GetError().Code == core::EErrorCode::ExternalFailure);
			}
		}

		CHECK(bSucceeded);

		return true;
	}

	bool run(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Dxc, memory);
		CHECK(test::Succeeded(compiler));
		CHECK(compiler.Value()->GetCapabilities().bDxil && compiler.Value()->GetCapabilities().bSpirV);
		CHECK(compiler.Value()->GetToolchainIdentity().AdapterVersion == 3);
		bool bComputeOnly = false;

		for (const shader::ShaderCompilerProfile& profile : compiler.Value()->GetProfiles())
		{
			if (profile.Target.Target == shader::EShaderTarget::SpirV)
			{
				CHECK(profile.StageMask == (core::uint64{1} << static_cast<core::uint32>(shader::EShaderStage::Compute)));
				bComputeOnly = true;
			}
		}

		CHECK(bComputeOnly);
		CHECK(profileRejections(*compiler.Value(), memory));
		shader::Shader artifact;
		CHECK(compile(*compiler.Value(), WITNESS, memory, artifact, true));
		CHECK(layouts(artifact, memory));
		const core::Result<core::uint32> selected = artifact.FindVariant(
			{"CSMain", shader::CompilerProfile(shader::EShaderSourceLanguage::Hlsl, shader::EShaderTarget::SpirV).Target});
		CHECK(test::Succeeded(selected));
		CHECK(malformed(artifact.GetVariants()[selected.Value()].Bytecode.View(), memory));
		shader::Shader unused;
		CHECK(compile(
			*compiler.Value(),
			"[[vk::binding(0,0)]] Buffer<float4> Input; [[vk::binding(1,0)]] RWStructuredBuffer<float4> Output; "
			"[numthreads(1,1,1)] void CSMain(uint3 id:SV_DispatchThreadID){ Output[id.x]=Input.Load(id.x); }",
			memory, unused, false, false, true));
		CHECK(compile(
			*compiler.Value(),
			"struct P{float2x2 M;}; [[vk::binding(0,0)]] ConstantBuffer<P> Parameters; [[vk::binding(1,0)]] "
			"RWStructuredBuffer<float> Output; [numthreads(1,1,1)] void CSMain(uint3 id:SV_DispatchThreadID){ "
			"Output[id.x]=Parameters.M[id.x&1][0]; }",
			memory, unused, false, false, true));
		CHECK(compile(
			*compiler.Value(),
			"[[vk::binding(0,0)]] Texture2D<float4> Inputs[2]; [[vk::binding(1,0)]] RWStructuredBuffer<float4> Output; "
			"[numthreads(1,1,1)] void CSMain(uint3 id:SV_DispatchThreadID){ "
			"Output[id.x]=Inputs[id.x&1].Load(int3(0,0,0)); }",
			memory, unused, false, false, true));
		CHECK(compile(
			*compiler.Value(),
			"[[vk::binding(0,0)]] StructuredBuffer<double> Input; [[vk::binding(1,0)]] RWStructuredBuffer<float> "
			"Output; [numthreads(1,1,1)] void CSMain(uint3 id:SV_DispatchThreadID){ Output[id.x]=float(Input[id.x]); }",
			memory, unused, false, false, true));
		CHECK(compile(
			*compiler.Value(),
			"[[vk::binding(0,0)]] StructuredBuffer<float> Input; [[vk::binding(1,0)]] RWStructuredBuffer<float> "
			"Output; [numthreads(1,1,1)] void CSMain(uint3 id:SV_DispatchThreadID){ min16float "
			"value=(min16float)Input[id.x]; Output[id.x]=value+min16float(0.125); }",
			memory, unused));
		CHECK(unused.GetVariants()[0].Requirements.Features == 0 && unused.GetVariants()[0].RequiredCapabilities == 0);

		return true;
	}
} // namespace

TEST(DxcSpirv, LayoutsFailuresAndCancellation)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(run(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
