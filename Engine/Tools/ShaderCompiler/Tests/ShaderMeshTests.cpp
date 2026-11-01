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
	constexpr core::StringView MESH_SOURCE = R"slang(struct Vertex
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};

struct Primitive
{
    nointerpolation uint Color : COLOR0;
};

[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint tid : SV_GroupIndex, out vertices Vertex vertices[3], out indices uint3 triangles[1], out primitives Primitive primitives[1])
{
    SetMeshOutputCounts(3, 1);
    vertices[0].Position = float4(-1, -1, 0, 1);
    vertices[1].Position = float4(0, 1, 0, 1);
    vertices[2].Position = float4(1, -1, 0, 1);
    vertices[0].UV = float2(0, 1);
    vertices[1].UV = float2(1, 1);
    vertices[2].UV = float2(2, 1);
    triangles[0] = uint3(0, 1, 2);
    primitives[0].Color = 7;
}

[shader("fragment")]
float4 PSMain(Vertex vertex) : SV_Target0
{
    return float4(vertex.UV.x / 2, vertex.UV.y, 0, 1);
}
		)slang";
	constexpr core::StringView TASK_SOURCE = R"slang(struct Payload { uint Tag; float4 Positions[3]; };
struct Vertex { float4 Position : SV_Position; float2 UV : TEXCOORD0; };
struct Primitive { nointerpolation uint Color : COLOR0; };
groupshared Payload taskData;
[shader("amplification")] [numthreads(1,1,1)]
void ASMain(uint tid : SV_GroupIndex)
{
    taskData.Tag = 7;
    taskData.Positions[0] = float4(-1,-1,0,1);
    taskData.Positions[1] = float4(0,1,0,1);
    taskData.Positions[2] = float4(1,-1,0,1);
    DispatchMesh(1,1,1,taskData);
}
[shader("mesh")] [numthreads(1,1,1)] [outputtopology("triangle")]
void MSMain(uint tid : SV_GroupIndex, in payload Payload data,
            out vertices Vertex verts[3], out indices uint3 triangles[1], out primitives Primitive primitives[1])
{
    SetMeshOutputCounts(3,1);
    for (uint i=0;i<3;++i) { verts[i].Position=data.Positions[i]; verts[i].UV=float2(i,1); }
    triangles[0]=uint3(0,1,2);
    primitives[0].Color=data.Tag;
}
[shader("fragment")]
float4 PSMain(Vertex vertex) : SV_Target0
{
    return float4(vertex.UV.x/2, vertex.UV.y, 0, 1);
		})slang";
	constexpr core::StringView FLOAT_PAYLOAD_SOURCE =
		R"slang(struct Payload { float Tag; float4 Positions[3]; };
struct Vertex { float4 Position : SV_Position; float2 UV : TEXCOORD0; };
struct Primitive { nointerpolation uint Color : COLOR0; };
groupshared Payload taskData;
[shader("amplification")] [numthreads(1,1,1)]
void ASMain(uint tid : SV_GroupIndex)
{
    taskData.Tag = 7;
    taskData.Positions[0] = float4(-1,-1,0,1);
    taskData.Positions[1] = float4(0,1,0,1);
    taskData.Positions[2] = float4(1,-1,0,1);
    DispatchMesh(1,1,1,taskData);
}
[shader("mesh")] [numthreads(1,1,1)] [outputtopology("triangle")]
void MSMain(uint tid : SV_GroupIndex, in payload Payload data,
            out vertices Vertex verts[3], out indices uint3 triangles[1], out primitives Primitive primitives[1])
{
    SetMeshOutputCounts(3,1);
    for (uint i=0;i<3;++i) { verts[i].Position=data.Positions[i]; verts[i].UV=float2(i,1); }
    triangles[0]=uint3(0,1,2);
    primitives[0].Color=data.Tag;
}
[shader("fragment")]
float4 PSMain(Vertex vertex) : SV_Target0
{
    return float4(vertex.UV.x/2, vertex.UV.y, 0, 1);
		})slang";
	constexpr core::StringView PRIMITIVE_SOURCE = R"slang(struct Payload { uint Tag; float4 Positions[3]; };
struct Vertex { float4 Position : SV_Position; float2 UV : TEXCOORD0; };
struct Primitive { nointerpolation uint Color : COLOR0; };
groupshared Payload taskData;
[shader("amplification")] [numthreads(1,1,1)]
void ASMain(uint tid : SV_GroupIndex)
{
    taskData.Tag = 7;
    taskData.Positions[0] = float4(-1,-1,0,1);
    taskData.Positions[1] = float4(0,1,0,1);
    taskData.Positions[2] = float4(1,-1,0,1);
    DispatchMesh(1,1,1,taskData);
}
[shader("mesh")] [numthreads(1,1,1)] [outputtopology("triangle")]
void MSMain(uint tid : SV_GroupIndex, in payload Payload data,
            out vertices Vertex verts[3], out indices uint3 triangles[1], out primitives Primitive primitives[1])
{
    SetMeshOutputCounts(3,1);
    for (uint i=0;i<3;++i) { verts[i].Position=data.Positions[i]; verts[i].UV=float2(i,1); }
    triangles[0]=uint3(0,1,2);
    primitives[0].Color=data.Tag;
}
[shader("fragment")]
float4 PSMain(Vertex vertex, nointerpolation uint color : COLOR0) : SV_Target0
{
    return float4(vertex.UV.x/2, color/7.0, 0, 1);
		})slang";

	bool compile(shader::EShaderTarget target, core::StringView source, bool bTask, core::MemoryResource& memory, shader::Shader& artifact, bool bUnsupported = false)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> root = shader::CreateShaderSourceSnapshot("mesh/profile.slang", source, memory);
		CHECK(test::Succeeded(root));
		request.Root = std::move(root).TakeValue();
		const core::StringView names[]{"MSMain", "PSMain", "ASMain"};
		const shader::EShaderStage stages[]{shader::EShaderStage::Mesh, shader::EShaderStage::Pixel, shader::EShaderStage::Task};

		for (core::uint32 index = 0; index < (bTask ? 3u : 2u); ++index)
		{
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign(names[index]));
			entry.Stage = stages[index];
			CHECK(request.EntryPoints.Add(std::move(entry)));
		}

		shader::ShaderTargetProfile profile;
		profile.Target = target;
		CHECK(request.Targets.Add(profile));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, resolver, report, memory);
		if (bUnsupported)
		{
			CHECK(!built && built.GetError().Code == core::EErrorCode::Unsupported);

			return true;
		}

		if (!built)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(built));
		artifact = built.Value().Artifact;

		return true;
	}

	bool selections(const shader::Shader& artifact, shader::ShaderEntrySelection (&output)[3], bool bTask)
	{
		for (core::uint32 index = 0; index < artifact.GetVariants().Size(); ++index)
		{
			const EShaderStage stage = artifact.GetEntryPoints()[artifact.GetVariants()[index].EntryPointIndex].Stage;
			const core::uint32 slot = stage == shader::EShaderStage::Mesh ? 0 : stage == shader::EShaderStage::Pixel ? 1 : 2;
			output[slot] = {artifact, index};
		}

		CHECK(output[0].Artifact.IsValid() && output[1].Artifact.IsValid());
		if (bTask)
		{
			CHECK(output[2].Artifact.IsValid());
		}

		return true;
	}

	bool verify(const shader::Shader& artifact, bool bTask, core::MemoryResource& memory)
	{
		CHECK(artifact.GetAbiVersion() == shader::SHADER_ABI_VERSION);
		core::Result<core::Array<core::Byte>> bytes = artifact.Save(shader::EShaderFormat::V1, memory);
		CHECK(test::Succeeded(bytes));
		core::Result<Shader> decoded = shader::Shader::Load(bytes.Value().View(), memory);
		CHECK(test::Succeeded(decoded));
		CHECK(decoded.Value().GetIdentity() == artifact.GetIdentity());
		CHECK(!artifact.Save(static_cast<shader::EShaderFormat>(2), memory));
		CHECK(!shader::Shader::Load({bytes.Value().Data(), bytes.Value().Size() - 1}, memory));
		shader::ShaderEntrySelection entries[3];
		CHECK(selections(decoded.Value(), entries, bTask));
		const shader::ShaderVariant& mesh = decoded.Value().GetVariants()[entries[0].VariantIndex];
		CHECK(mesh.MeshOutput.Topology == shader::EShaderMeshTopology::Triangles && mesh.MeshOutput.MaximumVertices == 3 && mesh.MeshOutput.MaximumPrimitives == 1);
		bool bVertex = false;
		bool bPrimitive = false;

		for (const shader::ShaderStageIO& output : mesh.StageOutputs)
		{
			bVertex |= output.Rate == shader::EShaderStageIORate::PerVertex;
			bPrimitive |= output.Rate == shader::EShaderStageIORate::PerPrimitive;
		}

		CHECK(bVertex && bPrimitive);
		core::Result<ShaderLayout> program = shader::ShaderLayout::Create({entries, bTask ? 3u : 2u}, memory);
		CHECK(test::Succeeded(program));
		CHECK(program.Value().GetStageIOStatus() == shader::EShaderStageIOStatus::Verified);
		if (bTask)
		{
			const shader::ShaderVariant& task = decoded.Value().GetVariants()[entries[2].VariantIndex];
			CHECK(!task.TaskPayload.Types.IsEmpty() && !mesh.TaskPayload.Types.IsEmpty());
			CHECK(!task.TaskPayload.bHasProvenByteSize && !mesh.TaskPayload.bHasProvenByteSize);
			CHECK((program.Value().GetFeatureRequirements().Features & static_cast<core::uint64>(shader::EShaderFeature::Task)) != 0);
			shader::ShaderLimits limits;
			limits.MaximumPayloadTypes = 1;
			CHECK(!shader::Shader::Load(bytes.Value().View(), memory, limits));
		}
		else
		{
			CHECK(mesh.TaskPayload.Types.IsEmpty());
		}

		return true;
	}

	bool run(core::MemoryResource& memory)
	{
		for (shader::EShaderTarget target : {shader::EShaderTarget::Dxil, shader::EShaderTarget::SpirV})
		{
			shader::Shader mesh;
			CHECK(compile(target, MESH_SOURCE, false, memory, mesh));
			CHECK(verify(mesh, false, memory));
		}

		shader::Shader task;
		CHECK(compile(shader::EShaderTarget::SpirV, TASK_SOURCE, true, memory, task));
		CHECK(verify(task, true, memory));
		shader::Shader incompatible;
		CHECK(compile(shader::EShaderTarget::SpirV, FLOAT_PAYLOAD_SOURCE, true, memory, incompatible));
		shader::ShaderEntrySelection original[3];
		shader::ShaderEntrySelection mixed[3];
		CHECK(selections(task, original, true) && selections(incompatible, mixed, true));
		mixed[2] = original[2];
		CHECK(!shader::ShaderLayout::Create(mixed, memory));
		shader::Shader primitive;
		CHECK(compile(shader::EShaderTarget::SpirV, PRIMITIVE_SOURCE, true, memory, primitive));
		shader::ShaderEntrySelection primitiveEntries[3];
		CHECK(selections(primitive, primitiveEntries, true));
		CHECK(!shader::ShaderLayout::Create(primitiveEntries, memory));
		shader::Shader nativeTask;
		CHECK(compile(shader::EShaderTarget::Dxil, TASK_SOURCE, true, memory, nativeTask));
		shader::ShaderEntrySelection nativeEntries[3];
		CHECK(selections(nativeTask, nativeEntries, true));
		core::Result<ShaderLayout> nativeProgram = shader::ShaderLayout::Create(nativeEntries, memory);
		CHECK(nativeProgram && nativeProgram.Value().GetTaskPayloadCompatibility() == shader::EShaderTaskPayloadCompatibility::NativeSizeVerified);

		return true;
	}
} // namespace

TEST(ShaderMesh, TaskPayloadAndMeshOutputs)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(run(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
