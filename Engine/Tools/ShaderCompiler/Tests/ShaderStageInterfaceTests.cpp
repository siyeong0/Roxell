#include "ShaderTestSources.h"

#include <ShaderBuilder.h>
#include <ShaderLayout.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include "ShaderCompilerTest.h"

namespace
{
	using namespace rox;
	using namespace core;
	using namespace shader;

	bool addIO(
		core::Array<shader::ShaderStageIO>& records,
		core::MemoryResource& memory,
		core::StringView name,
		core::uint32 location,
		shader::EShaderBuiltin builtin,
		shader::EShaderScalar scalar,
		core::uint32 width)
	{
		shader::ShaderStageIO value(memory);
		CHECK(value.Name.Assign(name));
		value.Location = location;
		value.Builtin = builtin;
		value.Scalar = scalar;
		value.VectorWidth = width;
		value.Interpolation = builtin == shader::EShaderBuiltin::None ? shader::EShaderInterpolation::Linear : shader::EShaderInterpolation::Unspecified;
		CHECK(records.Add(std::move(value)));

		return true;
	}

	bool sampleIndex(core::MemoryResource& memory)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot(
			"pipeline/sample-index.slang",
			"float4 PSMain(uint sampleIndex:SV_SampleIndex):SV_Target0 { return float4(sampleIndex,0,0,1); "
			"}\n",
			memory);
		CHECK(test::Succeeded(source));
		request.Root = std::move(source).TakeValue();
		shader::ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign("PSMain"));
		entry.Stage = shader::EShaderStage::Pixel;
		CHECK(request.EntryPoints.Add(std::move(entry)));
		shader::ShaderTargetProfile target;
		bool bProfileFound = false;

		for (const shader::ShaderCompilerProfile& profile : compiler.Value()->GetProfiles())
		{
			if (profile.Target.Target == shader::EShaderTarget::Dxil && profile.ShaderAbiVersion == shader::SHADER_ABI_VERSION)
			{
				target = profile.Target;
				bProfileFound = true;
				break;
			}
		}

		CHECK(bProfileFound && request.Targets.Add(target));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(request, resolver, report, memory);
		if (!compiled)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(compiled));
		const shader::ShaderVariant& native = compiled.Value().Artifact.GetVariants()[0];
		CHECK(native.StageInputs.Size() == 1);
		CHECK(
			native.StageInputs[0].Builtin == shader::EShaderBuiltin::SampleIndex && native.StageInputs[0].Location == shader::NO_STAGE_LOCATION &&
			native.StageInputs[0].Component == 0);
		shader::ShaderBuilder draft(memory);
		draft.AbiVersion = shader::SHADER_ABI_VERSION;
		draft.BuildProvenance = compiled.Value().BuildIdentity;
		shader::ShaderEntryPoint copiedEntry(memory);
		CHECK(copiedEntry.Name.Assign("PSMain"));
		copiedEntry.Stage = shader::EShaderStage::Pixel;
		CHECK(draft.EntryPoints.Add(std::move(copiedEntry)));
		shader::ShaderVariant variant(memory);
		variant.Target = native.Target;
		CHECK(variant.EmittedEntryPoint.Assign(native.EmittedEntryPoint));
		CHECK(variant.Bytecode.ResizeUninitialized(native.Bytecode.Size()));
		std::memcpy(variant.Bytecode.Data(), native.Bytecode.Data(), native.Bytecode.Size());
		variant.RequiredCapabilities = native.RequiredCapabilities;
		variant.Requirements.Features = native.RequiredCapabilities;
		variant.bStageIOComplete = true;
		CHECK(addIO(variant.StageInputs, memory, "sampleIndex", shader::NO_STAGE_LOCATION, shader::EShaderBuiltin::SampleIndex, shader::EShaderScalar::UnsignedInteger, 1));
		CHECK(addIO(variant.StageOutputs, memory, "color", 0, shader::EShaderBuiltin::None, shader::EShaderScalar::Float, 4));
		CHECK(variant.StageOutputs[0].Semantic.Assign("SV_TARGET"));
		CHECK(draft.Variants.Add(std::move(variant)));
		core::Result<Shader> artifact = shader::Shader::Create(draft, memory);
		CHECK(test::Succeeded(artifact));
		core::Result<core::Array<core::Byte>> encoded = artifact.Value().Save(memory);
		CHECK(test::Succeeded(encoded));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == artifact.Value().GetIdentity());
		shader::ShaderStageIO& input = draft.Variants[0].StageInputs[0];
		input.Location = 0;
		CHECK(!shader::Shader::Create(draft, memory));
		input.Location = shader::NO_STAGE_LOCATION;
		input.Scalar = shader::EShaderScalar::SignedInteger;
		CHECK(!shader::Shader::Create(draft, memory));
		input.Scalar = shader::EShaderScalar::UnsignedInteger;
		input.VectorWidth = 2;
		CHECK(!shader::Shader::Create(draft, memory));
		input.VectorWidth = 1;
		draft.EntryPoints[0].Stage = shader::EShaderStage::Vertex;
		CHECK(!shader::Shader::Create(draft, memory));

		return true;
	}

	bool run(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot(
			"pipeline/stages.slang",
			"struct V { float4 Position : SV_Position; float2 UV : TEXCOORD0; };\n"
			"V VSMain(uint id : SV_VertexID) { V v; v.Position=float4(id&1,id>>1,0,1); v.UV=float2(id,0); "
			"return v; }\n"
			"float4 PSMain(float2 uv : TEXCOORD0) : SV_Target0 { return float4(uv,0,1); }\n",
			memory);
		CHECK(source);
		request.Root = std::move(source).TakeValue();
		shader::ShaderEntryPoint vertex(memory);
		shader::ShaderEntryPoint pixel(memory);
		CHECK(vertex.Name.Assign("VSMain") && pixel.Name.Assign("PSMain"));
		vertex.Stage = shader::EShaderStage::Vertex;
		pixel.Stage = shader::EShaderStage::Pixel;
		CHECK(request.EntryPoints.Add(std::move(vertex)) && request.EntryPoints.Add(std::move(pixel)));
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
		// The test frontend supplies the explicit ABI for these known source declarations.
		shader::ShaderBuilder draft(memory);
		draft.AbiVersion = shader::SHADER_ABI_VERSION;
		draft.BuildProvenance = compiled.Value().BuildIdentity;

		for (const shader::ShaderEntryPoint& input : compiled.Value().Artifact.GetEntryPoints())
		{
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign(input.Name));
			entry.Stage = input.Stage;
			CHECK(draft.EntryPoints.Add(std::move(entry)));
		}

		core::uint32 vertexIndex = 0;
		core::uint32 pixelIndex = 0;

		for (const shader::ShaderVariant& input : compiled.Value().Artifact.GetVariants())
		{
			CHECK(input.Bindings.IsEmpty());
			shader::ShaderVariant variant(memory);
			variant.Target = input.Target;
			variant.EntryPointIndex = input.EntryPointIndex;
			CHECK(variant.EmittedEntryPoint.Assign(input.EmittedEntryPoint));
			CHECK(variant.Bytecode.ResizeUninitialized(input.Bytecode.Size()));
			std::memcpy(variant.Bytecode.Data(), input.Bytecode.Data(), input.Bytecode.Size());
			variant.RequiredCapabilities = input.RequiredCapabilities;
			variant.Requirements.Features = input.RequiredCapabilities;
			variant.bStageIOComplete = true;
			if (draft.EntryPoints[input.EntryPointIndex].Stage == shader::EShaderStage::Vertex)
			{
				vertexIndex = static_cast<core::uint32>(draft.Variants.Size());
				CHECK(addIO(variant.StageInputs, memory, "id", shader::NO_STAGE_LOCATION, shader::EShaderBuiltin::VertexIndex, shader::EShaderScalar::UnsignedInteger, 1));
				CHECK(addIO(variant.StageOutputs, memory, "Position", shader::NO_STAGE_LOCATION, shader::EShaderBuiltin::Position, shader::EShaderScalar::Float, 4));
				CHECK(addIO(variant.StageOutputs, memory, "UV", 0, shader::EShaderBuiltin::None, shader::EShaderScalar::Float, 2));
			}
			else
			{
				pixelIndex = static_cast<core::uint32>(draft.Variants.Size());
				CHECK(addIO(variant.StageInputs, memory, "uv", 0, shader::EShaderBuiltin::None, shader::EShaderScalar::Float, 2));
				variant.StageInputs[0].Rate = shader::EShaderStageIORate::PerVertex;
				CHECK(addIO(variant.StageOutputs, memory, "color", 0, shader::EShaderBuiltin::None, shader::EShaderScalar::Float, 4));
			}

			CHECK(draft.Variants.Add(std::move(variant)));
		}

		core::Result<Shader> artifact = shader::Shader::Create(draft, memory);
		CHECK(test::Succeeded(artifact));
		auto compose = [&](const shader::Shader& owner)
		{
			core::Result<core::uint32> vertexVariant = owner.FindVariant({"VSMain", target});
			core::Result<core::uint32> pixelVariant = owner.FindVariant({"PSMain", target});
			ASSERT(vertexVariant && pixelVariant, "Both known test entries must survive canonical publication.");
			shader::ShaderEntrySelection selections[]{{owner, vertexVariant.Value()}, {owner, pixelVariant.Value()}};

			return shader::ShaderLayout::Create(selections, memory);
		};

		core::Result<core::Array<core::Byte>> encoded = artifact.Value().Save(memory);
		CHECK(test::Succeeded(encoded));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == artifact.Value().GetIdentity());
		CHECK(compose(decoded.Value()));
		shader::ShaderLimits limits;
		limits.MaximumStageIO = 4;
		core::Result<Shader> limited = shader::Shader::Load(encoded.Value().View(), memory, limits);
		CHECK(!limited && limited.GetError().Code == core::EErrorCode::InvalidArgument);

		core::Result<ShaderLayout> linked = compose(artifact.Value());
		CHECK(test::Succeeded(linked));
		CHECK(linked.Value().GetStageIOStatus() == shader::EShaderStageIOStatus::Verified);
		{
			core::Result<ShaderBuilder> authoring = shader::ShaderBuilder::FromShader(artifact.Value(), memory);
			CHECK(test::Succeeded(authoring));
			CHECK(authoring.Value().Name.Assign("SurfaceShader"));
			CHECK(authoring.Value().AddKeyword("TINT"));
			shader::ShaderPass pass(memory);
			CHECK(pass.Name.Assign("Surface") && pass.Tag.Assign("Forward"));
			pass.RenderState.Cull = shader::EShaderCullMode::None;
			pass.RenderState.bBlend = true;
			pass.RenderState.SourceColor = shader::EShaderBlendFactor::SourceAlpha;
			pass.RenderState.DestinationColor = shader::EShaderBlendFactor::OneMinusSourceAlpha;

			for (core::uint32 i = 0; i < 2; ++i)
			{
				shader::ShaderCodeChoice choice(memory);
				core::String vertexName(memory);
				core::String pixelName(memory);
				CHECK(vertexName.Assign("VSMain") && pixelName.Assign("PSMain"));
				CHECK(choice.Entries.Add(std::move(vertexName)) && choice.Entries.Add(std::move(pixelName)));
				if (i == 1)
				{
					core::String keyword(memory);
					CHECK(keyword.Assign("TINT") && choice.Keywords.Add(std::move(keyword)));
				}

				CHECK(pass.Choices.Add(std::move(choice)));
			}

			CHECK(authoring.Value().AddPass(std::move(pass)));
			core::Result<Shader> authored = std::move(authoring).TakeValue().Build(memory);
			CHECK(test::Succeeded(authored));
			core::Result<core::Array<core::Byte>> assetBytes = authored.Value().Save(memory);
			CHECK(assetBytes);
			core::Result<Shader> restored = shader::Shader::Load(assetBytes.Value().View(), memory);
			CHECK(restored && restored.Value().GetIdentity() == authored.Value().GetIdentity());
			shader::ShaderOptions options(memory);
			CHECK(restored.Value().FindPass("Surface") && !restored.Value().FindKernel("Surface"));

			for (core::uint32 i = 0; i < 2; ++i)
			{
				CHECK(options.SetKeyword("TINT", i == 1));
				core::Result<ShaderPassSelection> selected = restored.Value().SelectPass("Surface", target, options, memory);
				CHECK(test::Succeeded(selected));
				CHECK(selected.Value().GetStages().Size() == 2);
				CHECK(selected.Value().GetLayout().GetStageIOStatus() == shader::EShaderStageIOStatus::Verified);
				CHECK(selected.Value().GetCodeHash() == linked.Value().GetSelectedCodeHash());
				CHECK(selected.Value().GetRenderState().bBlend && selected.Value().GetRenderState().Cull == shader::EShaderCullMode::None);
			}
		}

		draft.AbiVersion = shader::SHADER_ABI_VERSION;
		draft.Variants[pixelIndex].StageInputs[0].Rate = shader::EShaderStageIORate::PerVertex;
		core::Result<Shader> advanced = shader::Shader::Create(draft, memory);
		CHECK(advanced && compose(advanced.Value()));
		draft.Variants[pixelIndex].StageInputs[0].Rate = shader::EShaderStageIORate::PerPrimitive;
		core::Result<Shader> primitiveConsumer = shader::Shader::Create(draft, memory);
		CHECK(primitiveConsumer && !compose(primitiveConsumer.Value()));
		draft.AbiVersion = shader::SHADER_ABI_VERSION;
		draft.Variants[pixelIndex].StageInputs[0].Rate = shader::EShaderStageIORate::PerVertex;
		std::swap(draft.Variants[vertexIndex].StageOutputs[0], draft.Variants[vertexIndex].StageOutputs[1]);
		core::Result<Shader> reordered = shader::Shader::Create(draft, memory);
		CHECK(reordered && reordered.Value().GetIdentity() == artifact.Value().GetIdentity());
		draft.Variants[pixelIndex].StageInputs[0].Component = 0xffffffffu;
		CHECK(!shader::Shader::Create(draft, memory));
		draft.Variants[pixelIndex].StageInputs[0].Component = 0;
		draft.Variants[pixelIndex].StageInputs[0].VectorWidth = 3;
		core::Result<Shader> mismatched = shader::Shader::Create(draft, memory);
		CHECK(mismatched && !compose(mismatched.Value()));
		draft.Variants[pixelIndex].StageInputs[0].VectorWidth = 2;
		draft.Variants[vertexIndex].StageOutputs.RemoveAt(0);
		core::Result<Shader> missing = shader::Shader::Create(draft, memory);
		CHECK(missing && !compose(missing.Value()));
		draft.Variants[pixelIndex].StageInputs[0].Scalar = shader::EShaderScalar::UnsignedInteger;
		CHECK(!shader::Shader::Create(draft, memory));

		return true;
	}
} // namespace

TEST(ShaderStageInterface, StageMatching)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(run(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderStageInterface, SampleIndex)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(sampleIndex(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
