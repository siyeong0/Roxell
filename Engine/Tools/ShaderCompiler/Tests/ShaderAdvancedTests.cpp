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
	constexpr core::StringView RUNTIME_ARRAY = R"slang(
[[vk::binding(3,1)]] Texture2D<uint> resources[] : register(t3,space1);
[[vk::binding(0,0)]] RWStructuredBuffer<uint> output : register(u0,space0);
[shader("compute")][numthreads(1,1,1)]
void CSMain(uint3 tid:SV_DispatchThreadID) { output[0]=resources[tid.x].Load(int3(0,0,0)); }
		)slang";
	constexpr core::StringView FIXED_ARRAY = R"slang(
[[vk::binding(3,1)]] Texture2D<uint> resources[4] : register(t3,space1);
[[vk::binding(0,0)]] RWStructuredBuffer<uint> output : register(u0,space0);
[shader("compute")][numthreads(256,1,1)]
void CSMain(uint3 tid:SV_DispatchThreadID) { output[tid.x]=resources[tid.x % 4].Load(int3(0,0,0)); }
		)slang";
	constexpr core::StringView LOCAL_RECORD = R"slang(
struct Payload { uint value; };
struct LocalData { uint index; uint values[2]; };
[[vk::shader_record]] ConstantBuffer<LocalData> record;
[shader("closesthit")]
void CHMain(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{ payload.value=record.index+record.values[1]; }
		)slang";
	constexpr core::StringView RESOURCE_HEAP = R"slang(
RWStructuredBuffer<uint> output : register(u0,space0);
[shader("compute")][numthreads(1,1,1)]
void CSMain(uint3 tid:SV_DispatchThreadID)
{ ByteAddressBuffer data = ResourceDescriptorHeap[tid.x]; output[0]=data.Load(0); }
		)slang";
	constexpr core::StringView GRAPHICS_ARRAY = R"slang(
[[vk::binding(0,1)]] Texture2D<float4> textures[] : register(t0,space1);
[[vk::binding(0,2)]] Texture2D<float4> unusedTextures[] : register(t0,space2);
[[vk::binding(0,0)]] SamplerState surfaceSampler : register(s0);
[[vk::binding(1,0)]] StructuredBuffer<uint> selectors : register(t1);
struct Varying { float4 position : SV_Position; nointerpolation uint index : TEXCOORD0; };
Varying VSMain(float4 position : POSITION, uint index : TEXCOORD0)
{ Varying result; result.position=position; result.index=selectors[index]; return result; }
float4 PSMain(Varying input) : SV_Target0
{ return textures[NonUniformResourceIndex(input.index)].Sample(surfaceSampler,float2(0.5)); }
		)slang";

	bool compile(core::StringView source, shader::EShaderTarget target, bool ray, shader::Shader& artifact, core::MemoryResource& memory)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot("advanced/source.slang", source, memory);
		CHECK(test::Succeeded(snapshot));
		request.Root = std::move(snapshot).TakeValue();
		shader::ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign(ray ? core::StringView("CHMain") : core::StringView("CSMain")));
		entry.Stage = ray ? shader::EShaderStage::ClosestHit : shader::EShaderStage::Compute;
		CHECK(request.EntryPoints.Add(std::move(entry)));
		shader::ShaderTargetProfile profile;
		profile.Target = target;
		CHECK(request.Targets.Add(profile));
		if (ray)
		{
			request.RayDeclaredLimits = {4, 8, 0};
		}

		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, resolver, report, memory);
		if (!built)
		{
			std::fprintf(stderr, "Advanced compile target=%u ray=%u\n", static_cast<unsigned>(target), unsigned(ray));
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(built));
		artifact = built.Value().Artifact;
		CHECK(artifact.GetAbiVersion() == shader::SHADER_ABI_VERSION);

		return true;
	}

	bool roundTrip(const shader::Shader& artifact, core::MemoryResource& memory)
	{
		core::Result<core::Array<core::Byte>> encoded = artifact.Save(shader::EShaderFormat::V1, memory);
		CHECK(test::Succeeded(encoded));
		CHECK(!artifact.Save(static_cast<shader::EShaderFormat>(2), memory));
		core::Result<Shader> decoded = shader::Shader::Load(encoded.Value().View(), memory);
		CHECK(decoded && decoded.Value().GetIdentity() == artifact.GetIdentity());
		CHECK(!shader::Shader::Load({encoded.Value().Data(), encoded.Value().Size() - 1}, memory));
		core::Result<core::Array<core::Byte>> repeated = decoded.Value().Save(shader::EShaderFormat::V1, memory);
		CHECK(repeated && test::EqualBytes(encoded.Value().View(), repeated.Value().View()));

		return true;
	}

	bool arrays(shader::EShaderTarget target, core::MemoryResource& memory)
	{
		shader::Shader runtime;
		shader::Shader fixed;
		CHECK(compile(RUNTIME_ARRAY, target, false, runtime, memory));
		CHECK(compile(FIXED_ARRAY, target, false, fixed, memory));
		CHECK(roundTrip(runtime, memory) && roundTrip(fixed, memory));
		CHECK(fixed.GetEntryPoints()[0].ThreadGroupSize[0] == 256);
		const auto inspect = [](const shader::Shader& artifact, bool unbounded) -> bool
		{
			bool found = false;

			for (const shader::ShaderBinding& binding : artifact.GetVariants()[0].Bindings)
			{
				if (binding.Name.View() == "resources")
				{
					found = true;
					CHECK(binding.Binding == 3 && binding.Space == 1);
					CHECK(binding.ArrayCount == (unbounded ? 0u : 4u));
					CHECK(binding.CountMode == (unbounded ? shader::EShaderDescriptorCountMode::Runtime : shader::EShaderDescriptorCountMode::Fixed));
				}
				else
				{
					CHECK(binding.CountMode == shader::EShaderDescriptorCountMode::Fixed && binding.ArrayCount == 1);
				}
			}

			CHECK(found);

			return true;
		};

		CHECK(inspect(runtime, true) && inspect(fixed, false));
		const shader::ShaderEntrySelection a{runtime, 0};
		const shader::ShaderEntrySelection b{fixed, 0};
		core::Result<ShaderLayout> ia = shader::ShaderLayout::Create({&a, 1}, memory);
		core::Result<ShaderLayout> ib = shader::ShaderLayout::Create({&b, 1}, memory);
		CHECK(ia && ib && ia.Value().GetPhysicalLayoutHash() != ib.Value().GetPhysicalLayoutHash());
		core::Result<ShaderInterfaceCompatibility> compatibility = shader::CompareShaderLayouts(ia.Value(), ib.Value(), memory);
		CHECK(compatibility && compatibility.Value().Level == shader::EShaderInterfaceCompatibility::Incompatible);

		return true;
	}

	bool localRecord(shader::EShaderTarget target, core::MemoryResource& memory)
	{
		shader::Shader artifact;
		CHECK(compile(LOCAL_RECORD, target, true, artifact, memory) && roundTrip(artifact, memory));
		const shader::ShaderVariant& variant = artifact.GetVariants()[0];
		const ShaderLocalRecord& record = variant.LocalRecord;
		if (target == shader::EShaderTarget::Dxil)
		{
			// The Vulkan-only annotation becomes an ordinary DXIL global constant buffer.
			CHECK(record.Lowering == shader::EShaderLocalRecordLowering::None);
			bool found = false;

			for (const shader::ShaderBinding& binding : variant.Bindings)
			{
				found |= binding.Name.View() == "record" && binding.Kind == shader::EShaderBindingKind::ConstantBuffer;
			}

			CHECK(found);
		}
		else
		{
			CHECK(record.Lowering == shader::EShaderLocalRecordLowering::VulkanShaderRecord);
			CHECK(record.DataLayout < variant.Layouts.Size() && record.ByteSize == 48 && record.Alignment == 16);
			const shader::ShaderDataLayout& layout = variant.Layouts[record.DataLayout];
			CHECK(layout.ByteSize == record.ByteSize && layout.Alignment == record.Alignment);
			bool scalar = false;
			bool array = false;

			for (const shader::ShaderDataField& field : layout.Fields)
			{
				if (field.Name.View() == "index")
				{
					scalar = field.ByteOffset == 0 && field.ByteSize == 4;
				}

				if (field.Name.View() == "values")
				{
					array = field.ByteOffset == 16 && field.ArrayCount == 2 && field.ArrayStride == 16;
				}
			}

			CHECK(scalar && array);

			for (const shader::ShaderBinding& binding : variant.Bindings)
			{
				CHECK(binding.Name.View() != "record");
			}
		}

		return true;
	}

	bool bufferArrays(shader::EShaderTarget target, core::MemoryResource& memory)
	{
		for (const bool unbounded : {false, true})
		{
			shader::Shader artifact;
			CHECK(test::CompileDescriptorArrayShader(target, unbounded, memory, artifact));
			CHECK(roundTrip(artifact, memory));
			const shader::ShaderVariant& variant = artifact.GetVariants()[0];
			bool found = false;

			for (const shader::ShaderBinding& binding : variant.Bindings)
			{
				if (binding.Name.View() == "Inputs")
				{
					found = true;
					CHECK(binding.Space == 2 && binding.Binding == 0);
					CHECK(binding.ArrayCount == (unbounded ? 0u : 16u));
				}
			}

			CHECK(found);
		}

		return true;
	}

	bool graphicsArrays(shader::EShaderTarget target, core::MemoryResource& memory)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot("advanced/graphics.slang", GRAPHICS_ARRAY, memory);
		CHECK(test::Succeeded(snapshot));
		request.Root = std::move(snapshot).TakeValue();

		for (const shader::EShaderStage stage : {shader::EShaderStage::Vertex, shader::EShaderStage::Pixel})
		{
			shader::ShaderEntryPoint entry(memory);
			entry.Stage = stage;
			CHECK(entry.Name.Assign(stage == shader::EShaderStage::Vertex ? "VSMain" : "PSMain"));
			CHECK(request.EntryPoints.Add(std::move(entry)));
		}

		shader::ShaderTargetProfile profile;
		profile.Target = target;
		CHECK(request.Targets.Add(profile));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, resolver, report, memory);
		if (!built)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(built));
		const shader::Shader& artifact = built.Value().Artifact;
		CHECK(artifact.GetVariants().Size() == 2 && roundTrip(artifact, memory));

		for (const shader::ShaderVariant& variant : artifact.GetVariants())
		{
			const bool pixel = artifact.GetEntryPoints()[variant.EntryPointIndex].Stage == shader::EShaderStage::Pixel;
			bool found = false;

			for (const shader::ShaderBinding& binding : variant.Bindings)
			{
				CHECK(binding.Name.View() != "unusedTextures");
				if (binding.Name.View() == "textures")
				{
					found = true;
					CHECK(binding.CountMode == shader::EShaderDescriptorCountMode::Runtime && binding.ArrayCount == 0);
					CHECK(binding.Kind == shader::EShaderBindingKind::SampledTexture && binding.Space == 1 && binding.Binding == 0);
				}
			}

			CHECK(found == pixel);
			CHECK(variant.ResourceUses.Size() == variant.Bindings.Size());

			for (const shader::ShaderResourceUse& use : variant.ResourceUses)
			{
				CHECK(use.BindingIndex < variant.Bindings.Size());
				CHECK(use.Access == variant.Bindings[use.BindingIndex].Access);
			}

			if (pixel && target == shader::EShaderTarget::SpirV)
			{
				CHECK((variant.Requirements.Features & static_cast<core::uint64>(shader::EShaderFeature::NonUniformIndexing)) != 0);
			}

			if (!pixel && target == shader::EShaderTarget::Dxil)
			{
				CHECK((variant.Requirements.Features & static_cast<core::uint64>(shader::EShaderFeature::RuntimeDescriptorArray)) == 0);
			}
		}

		return true;
	}

	bool heap(core::MemoryResource& memory)
	{
		shader::Shader artifact;
		CHECK(compile(RESOURCE_HEAP, shader::EShaderTarget::Dxil, false, artifact, memory));
		CHECK(roundTrip(artifact, memory));
		const core::uint64 features = artifact.GetVariants()[0].Requirements.Features;
		CHECK((features & static_cast<core::uint64>(shader::EShaderFeature::ResourceDescriptorHeap)) != 0);
		CHECK((features & static_cast<core::uint64>(shader::EShaderFeature::SamplerDescriptorHeap)) == 0);

		return true;
	}
} // namespace

TEST(ShaderAdvanced, ArraysDxil)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(arrays(shader::EShaderTarget::Dxil, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderAdvanced, ArraysSpirV)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(arrays(shader::EShaderTarget::SpirV, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderAdvanced, BufferarraysDxil)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(bufferArrays(shader::EShaderTarget::Dxil, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderAdvanced, BufferarraysSpirV)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(bufferArrays(shader::EShaderTarget::SpirV, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderAdvanced, LocalrecordDxil)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(localRecord(shader::EShaderTarget::Dxil, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderAdvanced, LocalrecordSpirV)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(localRecord(shader::EShaderTarget::SpirV, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderAdvanced, GraphicsarraysDxil)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(graphicsArrays(shader::EShaderTarget::Dxil, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderAdvanced, GraphicsarraysSpirV)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(graphicsArrays(shader::EShaderTarget::SpirV, memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderAdvanced, DescriptorHeap)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(heap(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
