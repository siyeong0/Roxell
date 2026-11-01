#include "ShaderTestSources.h"

#include <FailingMemoryResource.h>
#include <ShaderBuilder.h>
#include <ShaderBuildPlan.h>
#include <ShaderLayout.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <limits>

#include "ShaderCompilerTest.h"

namespace
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	constexpr core::usize UNLIMITED_ALLOCATIONS = std::numeric_limits<core::usize>::max();
	constexpr core::StringView SOURCE = R"slang(
struct Payload { uint4 Value; };
struct LocalData { uint Index; uint Values[2]; };
RaytracingAccelerationStructure Scene : register(t0);
RWStructuredBuffer<uint4> Output : register(u1);
ConstantBuffer<LocalData> Record : register(b3,space9);
StructuredBuffer<uint> LocalInput : register(t7,space9);
[shader("raygeneration")] void RayMain()
{
    RayDesc ray;
    ray.Origin=float3(DispatchRaysIndex().x==0 ? 0.0 : 2.0,0,-1);
    ray.Direction=float3(0,0,1); ray.TMin=0; ray.TMax=10;
    Payload payload; payload.Value=uint4(0,0,0,0);
    TraceRay(Scene,RAY_FLAG_NONE,255,0,1,0,ray,payload);
    Output[DispatchRaysIndex().x]=payload.Value;
}
[shader("miss")] void MissMain(inout Payload payload) { payload.Value=uint4(1,2,3,4); }
[shader("closesthit")] void ClosestMain(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{ payload.Value=uint4(Record.Index,Record.Values[0],Record.Values[1],LocalInput[0]); }
		)slang";

	bool request(shader::ShaderCompileRequest& output, core::MemoryResource& memory)
	{
		core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot("localroot/rays.slang", SOURCE, memory);
		CHECK(test::Succeeded(source));
		output.Root = std::move(source).TakeValue();
		const core::StringView names[]{"RayMain", "MissMain", "ClosestMain"};
		const shader::EShaderStage stages[]{shader::EShaderStage::RayGeneration, shader::EShaderStage::Miss, shader::EShaderStage::ClosestHit};

		for (core::uint32 index = 0; index < 3; ++index)
		{
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign(names[index]));
			entry.Stage = stages[index];
			CHECK(output.EntryPoints.Add(std::move(entry)));
		}

		shader::ShaderTargetProfile target;
		target.Target = shader::EShaderTarget::Dxil;
		CHECK(output.Targets.Add(target));
		output.RayDeclaredLimits = {16, 8, 0};
		shader::ShaderLocalRootRecipe recipe(memory);
		CHECK(recipe.EntryPointName.Assign("ClosestMain"));
		shader::ShaderLocalRootArgumentDeclaration constants(memory);
		constants.Kind = shader::EShaderLocalRootArgumentKind::Constants32;
		constants.ValueCount = 9;
		CHECK(constants.BindingName.Assign("Record"));
		CHECK(recipe.Arguments.Add(std::move(constants)));
		shader::ShaderLocalRootArgumentDeclaration address(memory);
		address.Kind = shader::EShaderLocalRootArgumentKind::ShaderResourceAddress;
		CHECK(address.BindingName.Assign("LocalInput"));
		CHECK(recipe.Arguments.Add(std::move(address)));
		CHECK(output.LocalRootRecipes.Add(std::move(recipe)));

		return true;
	}

	bool verify(const shader::Shader& artifact, core::MemoryResource& memory)
	{
		CHECK(artifact.GetAbiVersion() == shader::SHADER_ABI_VERSION);
		core::Result<core::Array<core::Byte>> encoded = artifact.Save(shader::EShaderFormat::V1, memory);
		CHECK(test::Succeeded(encoded));
		CHECK(!artifact.Save(static_cast<shader::EShaderFormat>(2), memory));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == artifact.GetIdentity());
		core::Result<core::Array<core::Byte>> repeated = decoded.Value().Save(shader::EShaderFormat::V1, memory);
		CHECK(repeated && test::EqualBytes(encoded.Value().View(), repeated.Value().View()));
		shader::ShaderEntrySelection selections[3];
		CHECK(decoded.Value().GetVariants().Size() == 3);

		for (core::uint32 index = 0; index < 3; ++index)
		{
			selections[index] = {decoded.Value(), index};
			const shader::ShaderVariant& variant = decoded.Value().GetVariants()[index];
			const shader::ShaderEntryPoint& entry = decoded.Value().GetEntryPoints()[variant.EntryPointIndex];
			if (entry.Stage == shader::EShaderStage::ClosestHit)
			{
				CHECK(variant.Bindings.Size() == 2 && variant.LocalRootArguments.Size() == 2);
				CHECK(variant.LocalRecord.Lowering == shader::EShaderLocalRecordLowering::D3D12LocalRootArguments);
				CHECK(variant.LocalRecord.Proof == shader::EShaderLocalRecordProof::DeclaredDxilRecipe);
				CHECK(variant.LocalRecord.DataLayout == shader::NO_DATA_LAYOUT && variant.LocalRecord.ByteSize == 48 && variant.LocalRecord.Alignment == 8);
				const shader::ShaderLocalRootArgument& constants = variant.LocalRootArguments[0];
				const shader::ShaderLocalRootArgument& address = variant.LocalRootArguments[1];
				CHECK(constants.Kind == shader::EShaderLocalRootArgumentKind::Constants32 && constants.ByteOffset == 0 && constants.ValueCount == 9);
				CHECK(address.Kind == shader::EShaderLocalRootArgumentKind::ShaderResourceAddress && address.ByteOffset == 40 && address.ValueCount == 0);
				CHECK(constants.BindingIndex < variant.Bindings.Size() && address.BindingIndex < variant.Bindings.Size());
				CHECK(variant.Bindings[constants.BindingIndex].Name.View() == "Record");
				CHECK(variant.Bindings[address.BindingIndex].Name.View() == "LocalInput");

				for (const shader::ShaderBinding& binding : variant.Bindings)
				{
					CHECK(binding.Scope == shader::EShaderBindingScope::LocalRecord && binding.Space == 9);
				}
			}
			else
			{
				CHECK(variant.LocalRecord.Lowering == shader::EShaderLocalRecordLowering::None && variant.LocalRootArguments.IsEmpty());
				CHECK(variant.Bindings.Size() == (entry.Stage == shader::EShaderStage::RayGeneration ? 2u : 0u));

				for (const shader::ShaderBinding& binding : variant.Bindings)
				{
					CHECK(binding.Scope == shader::EShaderBindingScope::Global && binding.Space == 0);
				}
			}
		}

		core::Result<ShaderLayout> composed = shader::ShaderLayout::Create(selections, memory);
		CHECK(composed && composed.Value().GetBindings().Size() == 2);

		for (const shader::ShaderProgramBinding& binding : composed.Value().GetBindings())
		{
			CHECK(binding.Binding.Name.View() == "Scene" || binding.Binding.Name.View() == "Output");
		}

		return true;
	}

	bool run(core::FailingMemoryResource& memory)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest input(memory);
		CHECK(request(input, memory));
		test::MemoryShaderSources sources;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildPlan> plan = compiler.Value()->PrepareBuild(input, sources, report, memory);
		CHECK(test::Succeeded(plan));
		core::Result<core::Array<core::Byte>> proof = plan.Value().EncodeInputs(memory, 1u << 20);
		CHECK(test::Succeeded(proof));
		input.LocalRootRecipes[0].Arguments[0].ValueCount = 8;
		core::Result<ShaderBuildPlan> changed = compiler.Value()->PrepareBuild(input, sources, report, memory);
		CHECK(changed && changed.Value().GetBuildIdentity() != plan.Value().GetBuildIdentity());
		CHECK(!changed.Value().VerifyInputs(proof.Value().View(), 1u << 20));
		CHECK(!compiler.Value()->Compile(changed.Value(), report, memory));
		memory.Reset(0);
		core::Result<void> same = plan.Value().VerifyInputs(proof.Value().View(), 1u << 20);
		const usize attempts = memory.GetAllocationAttemptCount();
		memory.Reset(UNLIMITED_ALLOCATIONS);
		CHECK(same && attempts == 0);
		core::Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(plan.Value(), report, memory);
		if (!compiled)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(compiled));
		CHECK(verify(compiled.Value().Artifact, memory));
		input.LocalRootRecipes[0].Arguments[0].ValueCount = 10;
		core::Result<ShaderBuildOutput> padded = compiler.Value()->Compile(input, sources, report, memory);
		CHECK(test::Succeeded(padded));

		for (core::uint32 index = 0; index < compiled.Value().Artifact.GetVariants().Size(); ++index)
		{
			const shader::ShaderVariant& originalVariant = compiled.Value().Artifact.GetVariants()[index];
			if (originalVariant.LocalRootArguments.IsEmpty())
			{
				continue;
			}

			const shader::ShaderVariant& paddedVariant = padded.Value().Artifact.GetVariants()[index];
			// The extra constant replaces padding; total record size alone cannot establish compatibility.
			CHECK(originalVariant.LocalRecord.ByteSize == paddedVariant.LocalRecord.ByteSize);
			core::Result<core::Hash256> originalIdentity = shader::ComputeShaderLocalRecordPhysicalIdentity(originalVariant, memory);
			core::Result<core::Hash256> paddedIdentity = shader::ComputeShaderLocalRecordPhysicalIdentity(paddedVariant, memory);
			CHECK(originalIdentity && paddedIdentity && originalIdentity.Value() != paddedIdentity.Value());
			const shader::ShaderEntrySelection originalSelection{compiled.Value().Artifact, index};
			const shader::ShaderEntrySelection paddedSelection{padded.Value().Artifact, index};
			core::Result<ShaderLayout> originalInterface = shader::ShaderLayout::Create({&originalSelection, 1}, memory);
			core::Result<ShaderLayout> paddedInterface = shader::ShaderLayout::Create({&paddedSelection, 1}, memory);
			CHECK(originalInterface && paddedInterface);
			core::Result<ShaderInterfaceCompatibility> compatibility = shader::CompareShaderLayouts(originalInterface.Value(), paddedInterface.Value(), memory);
			CHECK(compatibility && compatibility.Value().Level == shader::EShaderInterfaceCompatibility::Incompatible);
		}

		input.LocalRootRecipes[0].Arguments[0].ValueCount = 9;
		CHECK(input.LocalRootRecipes[0].Arguments[1].BindingName.Assign("Missing"));
		CHECK(!compiler.Value()->Compile(input, sources, report, memory));
		CHECK(input.LocalRootRecipes[0].Arguments[1].BindingName.Assign("LocalInput"));
		input.LocalRootRecipes[0].Arguments[1].Kind = shader::EShaderLocalRootArgumentKind::ConstantBufferAddress;
		CHECK(!compiler.Value()->Compile(input, sources, report, memory));

		return true;
	}
} // namespace

TEST(ShaderLocalRoot, DeclaredRootsAndPacking)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
	core::FailingMemoryResource memory(tracking, UNLIMITED_ALLOCATIONS);

	EXPECT_TRUE(run(memory));
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}
