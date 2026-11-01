#include "DxilReflection.h"

#include <Assertion.h>
#include <ByteReader.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		class DxilMetadataReader final
		{
		public:
			DxilMetadataReader(MemoryResource&, CancellationToken);
			Result<void> Reflect(shader::ShaderVariant&, const shader::ShaderEntryPoint&, uint32 abi = shader::SHADER_ABI_VERSION);

		private:
			Result<void> readSignature(
				Span<const Byte> records,
				uint32 count,
				Span<const Byte> strings,
				Span<const Byte> indices,
				Array<shader::ShaderStageIO>& table,
				shader::EShaderStageIORate rate = shader::EShaderStageIORate::Unspecified);

		private:
			MemoryResource& m_Resource;
			CancellationToken m_Cancellation;
		};

	} // namespace

	namespace
	{
		Error unsupportedDxil()
		{
			return MakeBuildError(EErrorCode::Unsupported, "DXIL contains an unrepresented or incomplete signature contract.");
		}

		uint32 u32(Span<const Byte> bytes, usize offset)
		{
			ASSERT(offset <= bytes.Size() && bytes.Size() - offset >= 4, "DXIL word reads have checked bounds.");
			ByteReader reader({bytes.Data() + offset, 4});

			return reader.ReadU32LE();
		}

		uint32 u8(Span<const Byte> bytes, usize offset)
		{
			ASSERT(offset < bytes.Size(), "DXIL byte reads have checked bounds.");
			return ToInteger<uint32>(bytes[offset]);
		}

		StringView text(Span<const Byte> bytes, uint32 offset)
		{
			if (offset >= bytes.Size())
			{
				return {};
			}

			const char* begin = reinterpret_cast<const char*>(bytes.Data() + offset);

			for (usize index = 0; index < bytes.Size() - offset && index <= 4096; ++index)
			{
				if (begin[index] == '\0')
				{
					return {begin, index};
				}
			}

			return {};
		}

		Result<Span<const Byte>> psvPart(Span<const Byte> code)
		{
			using R = Result<Span<const Byte>>;

			if (code.Size() < 32 || code.Size() > 16u * 1024u * 1024u || u32(code, 0) != 0x43425844 || u32(code, 24) != code.Size())
			{
				return R::Failure(unsupportedDxil());
			}

			const uint32 count = u32(code, 28);
			if (count > 64 || count > (code.Size() - 32) / 4)
			{
				return R::Failure(unsupportedDxil());
			}

			Span<const Byte> result;

			for (uint32 index = 0; index < count; ++index)
			{
				const uint32 offset = u32(code, 32 + usize(index) * 4);
				if (offset < 32 + count * 4 || offset > code.Size() || code.Size() - offset < 8)
				{
					return R::Failure(unsupportedDxil());
				}

				const uint32 size = u32(code, offset + 4);
				if (size > code.Size() - offset - 8)
				{
					return R::Failure(unsupportedDxil());
				}

				if (u32(code, offset) == 0x30565350)
				{
					if (!result.IsEmpty())
					{
						return R::Failure(unsupportedDxil());
					}

					result = {code.Data() + offset + 8, size};
				}
			}

			return result.IsEmpty() ? R::Failure(unsupportedDxil()) : R::Success(result);
		}
	} // namespace

	DxilMetadataReader::DxilMetadataReader(MemoryResource& resource, CancellationToken cancellation)
		: m_Resource(resource)
		, m_Cancellation(cancellation)
	{
	}

	Result<void> DxilMetadataReader::readSignature(
		Span<const Byte> records,
		uint32 count,
		Span<const Byte> strings,
		Span<const Byte> indices,
		Array<shader::ShaderStageIO>& table,
		shader::EShaderStageIORate rate)
	{
		using R = Result<void>;

		ASSERT(count <= records.Size() / 16, "DXIL signature tables have checked record bounds.");

		for (uint32 index = 0; index < count; ++index)
		{
			if (m_Cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const Span<const Byte> record(records.Data() + usize(index) * 16, 16);
			shader::ShaderStageIO io(m_Resource);
			io.Rate = rate;
			const uint32 rows = u8(record, 8);
			const uint32 columnPacking = u8(record, 10);
			const uint32 columns = columnPacking & 15;
			const uint32 startColumn = (columnPacking >> 4) & 3;
			const bool bAllocated = (columnPacking & 64) != 0;
			if (bAllocated)
			{
				io.Location = u8(record, 9);
				io.Component = startColumn;
			}

			const uint32 semanticKind = u8(record, 11);
			const uint32 component = u8(record, 12);
			const uint32 interpolation = u8(record, 13);
			if (rows == 0 || columns == 0 || columns > 4 - startColumn || (columnPacking & 128) != 0 || u8(record, 14) >> 4 != 0 || u8(record, 15) != 0 || interpolation > 7)
			{
				return R::Failure(unsupportedDxil());
			}

			StringView semantic;

			switch (semanticKind)
			{
			case 0:
				semantic = text(strings, u32(record, 0));
				break;
			case 1:
				io.Builtin = shader::EShaderBuiltin::VertexIndex;
				semantic = StringView("SV_VERTEXID");
				break;
			case 2:
				io.Builtin = shader::EShaderBuiltin::InstanceIndex;
				semantic = StringView("SV_INSTANCEID");
				break;
			case 3:
				io.Builtin = shader::EShaderBuiltin::Position;
				semantic = StringView("SV_POSITION");
				break;
			case 4:
				io.Builtin = shader::EShaderBuiltin::RenderTargetArrayIndex;
				semantic = StringView("SV_RENDERTARGETARRAYINDEX");
				break;
			case 5:
				io.Builtin = shader::EShaderBuiltin::ViewportIndex;
				semantic = StringView("SV_VIEWPORTARRAYINDEX");
				break;
			case 10:
				io.Builtin = shader::EShaderBuiltin::PrimitiveIndex;
				semantic = StringView("SV_PRIMITIVEID");
				break;
			case 12:
				io.Builtin = shader::EShaderBuiltin::SampleIndex;
				semantic = StringView("SV_SAMPLEINDEX");
				break;
			case 13:
				io.Builtin = shader::EShaderBuiltin::FrontFace;
				semantic = StringView("SV_ISFRONTFACE");
				break;
			case 14:
				io.Builtin = shader::EShaderBuiltin::SampleMask;
				semantic = StringView("SV_COVERAGE");
				break;
			case 16:
				semantic = StringView("SV_TARGET");
				break;
			case 17:
			case 18:
			case 19:
				io.Builtin = shader::EShaderBuiltin::FragDepth;
				semantic = StringView("SV_DEPTH");
				if (rows != 1 || columns != 1 || component != 3 || bAllocated || startColumn != 0 || interpolation != 0)
				{
					return R::Failure(unsupportedDxil());
				}

				break;
			default:
				return R::Failure(unsupportedDxil());
			}

			if (semantic.IsEmpty() || !IsValidUtf8(semantic))
			{
				return R::Failure(unsupportedDxil());
			}

			if (!io.Name.Assign(semantic))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			if (io.Builtin == shader::EShaderBuiltin::None)
			{
				String uppercase(m_Resource);

				for (usize character = 0; character < semantic.Size(); ++character)
				{
					const char original = semantic[character];
					const char canonical = original >= 'a' && original <= 'z' ? static_cast<char>(original - 'a' + 'A') : original;
					if (!uppercase.Append({&canonical, 1}))
					{
						return R::Failure(MakeBuildAllocationError());
					}
				}

				io.Semantic = Move(uppercase);
				const uint32 first = u32(record, 4);
				if (first > indices.Size() / 4 || rows > indices.Size() / 4 - first)
				{
					return R::Failure(unsupportedDxil());
				}

				io.SemanticIndex = u32(indices, usize(first) * 4);

				for (uint32 row = 1; row < rows; ++row)
				{
					if (uint64(io.SemanticIndex) + row > 0xffffffffu || u32(indices, usize(first + row) * 4) != io.SemanticIndex + row)
					{
						return R::Failure(unsupportedDxil());
					}
				}

				char number[16]{};
				const ToCharsResult converted = ToChars(number, number + sizeof(number), io.SemanticIndex);
				if (!io.Name.Append({number, static_cast<usize>(converted.ptr - number)}))
				{
					return R::Failure(MakeBuildAllocationError());
				}
			}

			io.ArrayCount = rows;
			io.VectorWidth = columns;
			if (component == 1)
			{
				io.Scalar = shader::EShaderScalar::UnsignedInteger;
			}
			else if (component == 2)
			{
				io.Scalar = shader::EShaderScalar::SignedInteger;
			}
			else if (component == 3)
			{
				io.Scalar = shader::EShaderScalar::Float;
			}
			else if (component != 0 || io.Builtin != shader::EShaderBuiltin::FrontFace)
			{
				return R::Failure(unsupportedDxil());
			}

			if (io.Builtin == shader::EShaderBuiltin::FrontFace)
			{
				io.Scalar = shader::EShaderScalar::Bool;
			}

			io.Interpolation = interpolation == 0								 ? shader::EShaderInterpolation::Unspecified
				: interpolation == 1											 ? shader::EShaderInterpolation::Flat
				: interpolation == 4 || interpolation == 5 || interpolation == 7 ? shader::EShaderInterpolation::NoPerspective
																				: shader::EShaderInterpolation::Linear;
			io.bCentroid = interpolation == 3 || interpolation == 5;
			io.bSample = interpolation == 6 || interpolation == 7;
			if (!table.Add(Move(io)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		return R::Success();
	}

	Result<void> DxilMetadataReader::Reflect(shader::ShaderVariant& variant, const shader::ShaderEntryPoint& entry, [[maybe_unused]] uint32 abi)
	{
		using R = Result<void>;

		if (m_Cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		Result<Span<const Byte>> found = psvPart(variant.Bytecode.View());
		if (!found)
		{
			return R::Failure(Move(found.GetError()));
		}

		const Span<const Byte> bytes = found.Value();
		ByteReader reader(bytes);
		if (!reader.CanRead(4))
		{
			return R::Failure(unsupportedDxil());
		}

		const uint32 runtimeSize = reader.ReadU32LE();
		if ((runtimeSize != 36 && runtimeSize != 48 && runtimeSize != 52 && runtimeSize != 56) || !reader.CanRead(runtimeSize))
		{
			return R::Failure(unsupportedDxil());
		}

		const Span<const Byte> runtime = reader.ReadBytes(runtimeSize);
		if (u32(runtime, 16) != 0 || u32(runtime, 20) != 0xffffffffu)
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Explicit DXIL wave-size requirements need a supported native profile."));
		}

		const bool bMesh = entry.Stage == shader::EShaderStage::Mesh;
		const bool bTask = entry.Stage == shader::EShaderStage::Task;
		if (bTask)
		{
			if (runtimeSize < 48 || u32(runtime, 0) > 16384 || u8(runtime, 28) != 0 || u8(runtime, 29) != 0)
			{
				return R::Failure(unsupportedDxil());
			}

			const uint32 payloadBytes = u32(runtime, 0);
			if (payloadBytes != 0)
			{
				variant.TaskPayload.Proof = shader::EShaderTaskPayloadProof::NativeSize;
				variant.TaskPayload.bHasProvenByteSize = true;
				variant.TaskPayload.ProvenByteSize = payloadBytes;
			}

			variant.RequiredCapabilities |= static_cast<uint64>(shader::EShaderCapability::Mesh);
		}

		if (bMesh)
		{
			if (runtimeSize < 48 || u32(runtime, 4) != 0 || u8(runtime, 27) != 2)
			{
				return R::Failure(unsupportedDxil());
			}

			{
				const uint32 payloadBytes = u32(runtime, 8);
				if (payloadBytes > 16384 || u32(runtime, 0) > 28672 - payloadBytes || (payloadBytes != 0) != (variant.TaskPayload.RootType != shader::NO_PAYLOAD_TYPE))
				{
					return R::Failure(unsupportedDxil());
				}

				if (payloadBytes != 0)
				{
					variant.TaskPayload.bHasProvenByteSize = true;
					variant.TaskPayload.ProvenByteSize = payloadBytes;
				}
			}

			variant.MeshOutput.Topology = shader::EShaderMeshTopology::Triangles;
			variant.MeshOutput.MaximumVertices = u8(runtime, 12) | (u8(runtime, 13) << 8);
			variant.MeshOutput.MaximumPrimitives = u8(runtime, 14) | (u8(runtime, 15) << 8);
			variant.RequiredCapabilities |= static_cast<uint64>(shader::EShaderCapability::Mesh);
		}

		const uint32 expected = bTask ? 14u : bMesh ? 13u : entry.Stage == shader::EShaderStage::Vertex ? 1u : entry.Stage == shader::EShaderStage::Pixel ? 0u : 5u;
		if (u8(runtime, 24) != expected || u8(runtime, 25) != 0 || (!bMesh && u8(runtime, 30) != 0) || !reader.CanRead(4))
		{
			return R::Failure(unsupportedDxil());
		}

		const uint32 resourceCount = reader.ReadU32LE();
		if (resourceCount > 4096)
		{
			return R::Failure(unsupportedDxil());
		}

		if (resourceCount != 0)
		{
			if (!reader.CanRead(4))
			{
				return R::Failure(unsupportedDxil());
			}

			const uint32 resourceSize = reader.ReadU32LE();
			if ((resourceSize != 16 && resourceSize != 24) || resourceCount > reader.GetRemaining() / resourceSize)
			{
				return R::Failure(unsupportedDxil());
			}

			(void)reader.ReadBytes(usize(resourceCount) * resourceSize);
		}

		if (!reader.CanRead(4))
		{
			return R::Failure(unsupportedDxil());
		}

		const uint32 stringSize = reader.ReadU32LE();
		if (stringSize > 1024u * 1024u || stringSize % 4 != 0 || !reader.CanRead(stringSize))
		{
			return R::Failure(unsupportedDxil());
		}

		const Span<const Byte> strings = reader.ReadBytes(stringSize);
		if (!reader.CanRead(4))
		{
			return R::Failure(unsupportedDxil());
		}

		const uint32 indexCount = reader.ReadU32LE();
		if (indexCount > 4096 || indexCount > reader.GetRemaining() / 4)
		{
			return R::Failure(unsupportedDxil());
		}

		const Span<const Byte> indices = reader.ReadBytes(usize(indexCount) * 4);
		const uint32 inputCount = u8(runtime, 28);
		const uint32 outputCount = u8(runtime, 29);
		const uint32 primitiveCount = bMesh ? u8(runtime, 30) : 0;
		if (inputCount + outputCount + primitiveCount != 0)
		{
			if (!reader.CanRead(4) || reader.ReadU32LE() != 16 || inputCount + outputCount + primitiveCount > reader.GetRemaining() / 16)
			{
				return R::Failure(unsupportedDxil());
			}

			Result<void> inputs = readSignature(reader.ReadBytes(usize(inputCount) * 16), inputCount, strings, indices, variant.StageInputs);
			if (!inputs)
			{
				return inputs;
			}

			Result<void> outputs = readSignature(
				reader.ReadBytes(usize(outputCount) * 16), outputCount, strings, indices, variant.StageOutputs,
				bMesh ? shader::EShaderStageIORate::PerVertex : shader::EShaderStageIORate::Unspecified);
			if (!outputs)
			{
				return outputs;
			}

			Result<void> primitives = readSignature(
				reader.ReadBytes(usize(primitiveCount) * 16), primitiveCount, strings, indices, variant.StageOutputs, shader::EShaderStageIORate::PerPrimitive);
			if (!primitives)
			{
				return primitives;
			}
		}

		if (entry.Stage == shader::EShaderStage::Compute || bMesh || bTask)
		{
			if (runtimeSize < 48)
			{
				return R::Failure(unsupportedDxil());
			}

			for (uint32 axis = 0; axis < 3; ++axis)
			{
				if (u32(runtime, 36 + axis * 4) != entry.ThreadGroupSize[axis])
				{
					return R::Failure(unsupportedDxil());
				}
			}
		}

		for (const ShaderStageIO& input : variant.StageInputs)
		{
			if (input.Builtin == shader::EShaderBuiltin::FragDepth)
			{
				return R::Failure(unsupportedDxil());
			}
		}

		bool bDepthOutput = false;

		for (const ShaderStageIO& output : variant.StageOutputs)
		{
			if (output.Builtin == shader::EShaderBuiltin::FragDepth)
			{
				if (entry.Stage != shader::EShaderStage::Pixel || bDepthOutput)
				{
					return R::Failure(unsupportedDxil());
				}

				bDepthOutput = true;
			}
		}

		variant.Requirements.Features |= variant.RequiredCapabilities;
		if (bTask)
		{
			variant.Requirements.Features |= static_cast<uint64>(shader::EShaderFeature::Task);
		}

		uint32 workgroup = 0;
		if (entry.Stage == shader::EShaderStage::Compute || bMesh || bTask)
		{
			workgroup = 1;

			for (const uint32 size : entry.ThreadGroupSize)
			{
				if (size == 0 || size > ((bMesh || bTask) ? 128u : 1024u) / workgroup)
				{
					return R::Failure(unsupportedDxil());
				}

				workgroup *= size;
			}
		}

		variant.Requirements.MinimumWorkgroupInvocations = workgroup;
		variant.bStageIOComplete = true;

		return R::Success();
	}

	Result<void> ReflectDxilMetadata(ShaderVariant& variant, const ShaderEntryPoint& entry, MemoryResource& resource, CancellationToken cancellation, uint32 abi)
	{
		DxilMetadataReader reader(resource, cancellation);

		return reader.Reflect(variant, entry, abi);
	}
} // namespace rox::shader
