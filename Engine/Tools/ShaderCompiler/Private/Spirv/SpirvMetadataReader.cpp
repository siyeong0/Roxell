#include "SpirvMetadataReader.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteReader.h>
#include <Comparison.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		Error unsupportedSpirv()
		{
			return MakeBuildError(EErrorCode::Unsupported, "SPIR-V contains an unrepresented or unresolved interface contract.");
		}

		Result<shader::EShaderBuiltin> builtin(uint32 value)
		{
			using R = Result<shader::EShaderBuiltin>;

			switch (value)
			{
			case 0:
			case 15:
				return R::Success(shader::EShaderBuiltin::Position);
			case 7:
				return R::Success(shader::EShaderBuiltin::PrimitiveIndex);
			case 9:
				return R::Success(shader::EShaderBuiltin::RenderTargetArrayIndex);
			case 10:
				return R::Success(shader::EShaderBuiltin::ViewportIndex);
			case 17:
				return R::Success(shader::EShaderBuiltin::FrontFace);
			case 18:
				return R::Success(shader::EShaderBuiltin::SampleIndex);
			case 20:
				return R::Success(shader::EShaderBuiltin::SampleMask);
			case 22:
				return R::Success(shader::EShaderBuiltin::FragDepth);
			case 42:
				return R::Success(shader::EShaderBuiltin::VertexIndex);
			case 43:
				return R::Success(shader::EShaderBuiltin::InstanceIndex);
			case 4424:
				return R::Success(shader::EShaderBuiltin::BaseVertex);
			case 4425:
				return R::Success(shader::EShaderBuiltin::BaseInstance);
			case 4426:
				return R::Success(shader::EShaderBuiltin::DrawIndex);
			default:
				return R::Failure(unsupportedSpirv());
			}
		}
	} // namespace

	SpirvMetadataReader::SpirvMetadataReader(MemoryResource& resource, CancellationToken cancellation)
		: m_Resource(resource)
		, m_Cancellation(cancellation)
		, m_Nodes(resource)
		, m_Members(resource)
		, m_Interfaces(resource)
	{
	}

	uint32 SpirvMetadataReader::word(uint32 offset) const noexcept
	{
		ASSERT(offset < m_WordCount, "SPIR-V reads stay within checked instruction bounds.");
		ByteReader reader({m_Code.Data() + usize(offset) * 4, 4});

		return reader.ReadU32LE();
	}

	StringView SpirvMetadataReader::text(uint32 offset, uint32 end) const noexcept
	{
		ASSERT(offset <= end && end <= m_WordCount, "SPIR-V string bounds are checked.");
		const char* begin = reinterpret_cast<const char*>(m_Code.Data() + usize(offset) * 4);
		const usize bytes = usize(end - offset) * 4;

		for (usize index = 0; index < bytes && index <= 4096; ++index)
		{
			if (begin[index] == '\0')
			{
				return {begin, index};
			}
		}

		return {};
	}

	bool SpirvMetadataReader::decorate(SpirvDecoration& value, uint32 kind, uint32 argument)
	{
		switch (kind)
		{
		case 11:
			value.Builtin = argument;
			break;
		case 5271:
			if (value.bPerPrimitive)
			{
				return false;
			}

			value.bPerPrimitive = true;
			break;
		case 13:
			value.bNoPerspective = true;
			break;
		case 14:
			value.bFlat = true;
			break;
		case 16:
			value.bCentroid = true;
			break;
		case 17:
			value.bSample = true;
			break;
		case 30:
			value.Location = argument;
			break;
		case 31:
			value.Component = argument;
			value.bHasComponent = true;
			break;
		case 1:
			if (value.bHasSpecID)
			{
				return false;
			}

			value.SpecID = argument;
			value.bHasSpecID = true;
			break;
		case 2:
			value.bBlock = true;
			break;
		case 6:
			if (value.bHasArrayStride)
			{
				return false;
			}

			value.ArrayStride = argument;
			value.bHasArrayStride = true;
			break;
		case 35:
			if (value.bHasByteOffset)
			{
				return false;
			}

			value.ByteOffset = argument;
			value.bHasByteOffset = true;
			break;
		case 33:
			if (value.DescriptorBinding != 0xffffffffu)
			{
				return false;
			}

			value.DescriptorBinding = argument;
			break;
		case 34:
			if (value.DescriptorSet != 0xffffffffu)
			{
				return false;
			}

			value.DescriptorSet = argument;
			break;
		case 5300:
			break;
		case 0:
		case 3:
		case 4:
		case 5:
		case 7:
		case 19:
		case 20:
		case 21:
		case 23:
		case 24:
		case 25:
		case 26:
		case 29:
		case 44:
			break;
		default:
			value.bUnsupported = true;
			break;
		}

		return true;
	}

	Result<void> SpirvMetadataReader::indexInstructions(const shader::ShaderVariant& variant, const shader::ShaderEntryPoint& entry)
	{
		using R = Result<void>;

		bool bFoundEntry = false;

		for (uint32 offset = 5; offset < m_WordCount;)
		{
			if (m_Cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const uint32 instruction = word(offset);
			const uint32 count = instruction >> 16;
			const uint32 op = instruction & 0xffffu;
			if (count == 0 || count > m_WordCount - offset)
			{
				return R::Failure(unsupportedSpirv());
			}

			uint32 id = 0;
			if ((op >= 19 && op <= 33 && count >= 2) || (op == 5341 && count == 2))
			{
				id = word(offset + 1);
			}
			else if ((op == 43 || op == 50 || op == 59) && count >= 3)
			{
				id = word(offset + 2);
			}

			if (id != 0)
			{
				if (id >= m_Nodes.Size() || m_Nodes[id].Definition != 0)
				{
					return R::Failure(unsupportedSpirv());
				}

				m_Nodes[id].Definition = offset;
			}

			if (op == 17)
			{
				if (count != 2)
				{
					return R::Failure(unsupportedSpirv());
				}

				{
					const uint32 capability = word(offset + 1);

					switch (capability)
					{
					case 61:
						if (entry.Stage != shader::EShaderStage::Compute && entry.Stage != shader::EShaderStage::Pixel)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Subgroup);
						break;
					case 62:
						if (entry.Stage != shader::EShaderStage::Compute)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Subgroup) | static_cast<uint64>(shader::EShaderFeature::SubgroupVote);
						break;
					case 63:
						if (entry.Stage != shader::EShaderStage::Compute)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Subgroup) | static_cast<uint64>(shader::EShaderFeature::SubgroupArithmetic);
						break;
					case 64:
						if (entry.Stage != shader::EShaderStage::Compute)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Subgroup) | static_cast<uint64>(shader::EShaderFeature::SubgroupBallot);
						break;
					case 65:
						if (entry.Stage != shader::EShaderStage::Compute)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Subgroup) | static_cast<uint64>(shader::EShaderFeature::SubgroupShuffle);
						break;
					case 66:
						if (entry.Stage != shader::EShaderStage::Compute)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Subgroup) | static_cast<uint64>(shader::EShaderFeature::SubgroupShuffleRelative);
						break;
					case 67:
						if (entry.Stage != shader::EShaderStage::Compute)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Subgroup) | static_cast<uint64>(shader::EShaderFeature::SubgroupClustered);
						break;
					case 68:
						if (entry.Stage != shader::EShaderStage::Compute && entry.Stage != shader::EShaderStage::Pixel)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Subgroup) | static_cast<uint64>(shader::EShaderFeature::SubgroupQuad);
						break;
					case 25:
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::ImageGatherExtended);
						break;
					case 49:
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::StorageImageExtendedFormats);
						break;
					case 55:
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::StorageImageReadWithoutFormat);
						break;
					case 56:
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::StorageImageWriteWithoutFormat);
						break;
					case 28:
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::UniformBufferArrayDynamicIndexing);
						break;
					case 29:
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::SampledImageArrayDynamicIndexing);
						break;
					case 30:
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::StorageBufferArrayDynamicIndexing);
						break;
					case 31:
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::StorageImageArrayDynamicIndexing);
						break;
					default:
						break;
					}

					if (capability == 5302)
					{
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::RuntimeDescriptorArray);
					}

					if (capability == 5301 || (capability >= 5306 && capability <= 5312))
					{
						m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::NonUniformIndexing);
					}
				}

				if (word(offset + 1) == 12)
				{
					m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Int64Atomics);
				}

				if (word(offset + 1) == 5379)
				{
					if (entry.Stage != shader::EShaderStage::Pixel)
					{
						return R::Failure(unsupportedSpirv());
					}

					m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::DemoteToHelperInvocation);
				}

				if (word(offset + 1) == 35)
				{
					if (entry.Stage != shader::EShaderStage::Pixel)
					{
						return R::Failure(unsupportedSpirv());
					}

					m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::SampleRateShading);
				}
			}
			else if (op == 5 && count >= 3)
			{
				const uint32 named = word(offset + 1);
				if (named >= m_Nodes.Size() || text(offset + 2, offset + count).IsEmpty())
				{
					return R::Failure(unsupportedSpirv());
				}

				m_Nodes[named].NameOffset = offset;
			}
			else if (op == 71 && count >= 3)
			{
				const uint32 decorated = word(offset + 1);
				const uint32 kind = word(offset + 2);
				if (((kind == 1 || kind == 6 || kind == 11 || kind == 30 || kind == 31 || kind == 35) && count != 4) ||
					((kind == 2 || kind == 13 || kind == 14 || kind == 16 || kind == 17 || kind == 5271) && count != 3))
				{
					return R::Failure(unsupportedSpirv());
				}

				if (decorated >= m_Nodes.Size() || !decorate(m_Nodes[decorated].Decoration, word(offset + 2), count > 3 ? word(offset + 3) : 0))
				{
					return R::Failure(unsupportedSpirv());
				}
			}
			else if ((op == 72 && count >= 4) || (op == 6 && count >= 4))
			{
				const uint32 type = word(offset + 1);
				const uint32 member = word(offset + 2);
				if (type >= m_Nodes.Size() || m_Members.Size() >= 4096)
				{
					return R::Failure(unsupportedSpirv());
				}

				SpirvMemberDecoration value;
				value.Type = type;
				value.Member = member;
				value.Kind = op == 72 ? word(offset + 3) : 0xffffffffu;
				if (op == 6)
				{
					value.NameOffset = offset;
				}
				else if (word(offset + 3) == 2 ||
					((word(offset + 3) == 1 || word(offset + 3) == 6 || word(offset + 3) == 11 || word(offset + 3) == 30 || word(offset + 3) == 31 || word(offset + 3) == 35) &&
						count != 5) ||
					((word(offset + 3) == 13 || word(offset + 3) == 14 || word(offset + 3) == 16 || word(offset + 3) == 17 || word(offset + 3) == 5271) && count != 4) ||
					!decorate(value.Decoration, word(offset + 3), count > 4 ? word(offset + 4) : 0))
				{
					return R::Failure(unsupportedSpirv());
				}

				if (!m_Members.Add(value))
				{
					return R::Failure(MakeBuildAllocationError());
				}
			}
			else if (op == 15 && count >= 4)
			{
				const StringView name = text(offset + 3, offset + count);
				if (name == variant.EmittedEntryPoint.View())
				{
					const uint32 model = entry.Stage == shader::EShaderStage::Vertex ? 0u
						: entry.Stage == shader::EShaderStage::Pixel				 ? 4u
						: entry.Stage == shader::EShaderStage::Task					 ? 5364u
						: entry.Stage == shader::EShaderStage::Mesh					 ? 5365u
						: entry.Stage == shader::EShaderStage::RayGeneration		 ? 5313u
						: entry.Stage == shader::EShaderStage::Intersection			 ? 5314u
						: entry.Stage == shader::EShaderStage::AnyHit				 ? 5315u
						: entry.Stage == shader::EShaderStage::ClosestHit			 ? 5316u
						: entry.Stage == shader::EShaderStage::Miss					 ? 5317u
						: entry.Stage == shader::EShaderStage::Callable				 ? 5318u
																					: 5u;
					if (bFoundEntry || word(offset + 1) != model)
					{
						return R::Failure(unsupportedSpirv());
					}

					bFoundEntry = true;
					m_EntryID = word(offset + 2);
					const uint32 first = offset + 3 + static_cast<uint32>((name.Size() + 4) / 4);

					for (uint32 index = first; index < offset + count; ++index)
					{
						if (m_Interfaces.Size() >= 4096)
						{
							return R::Failure(unsupportedSpirv());
						}

						if (!m_Interfaces.Add(word(index)))
						{
							return R::Failure(MakeBuildAllocationError());
						}
					}
				}
			}
			else if (op == 16 && count >= 3 && word(offset + 1) == m_EntryID)
			{
				const uint32 mode = word(offset + 2);
				if (entry.Stage == shader::EShaderStage::Mesh && (mode == 26 || mode == 5270 || mode == 5298))
				{
					if (mode == 5298)
					{
						if (count != 3 || m_bTopology)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_bTopology = true;
						m_MeshOutput.Topology = shader::EShaderMeshTopology::Triangles;
					}
					else
					{
						uint32& maximum = mode == 26 ? m_MeshOutput.MaximumVertices : m_MeshOutput.MaximumPrimitives;
						if (count != 4 || maximum != 0 || word(offset + 3) == 0 || word(offset + 3) > 256)
						{
							return R::Failure(unsupportedSpirv());
						}

						maximum = word(offset + 3);
					}

					offset += count;
					continue;
				}

				if (mode != 17)
				{
					if (count != 3 ||
						(mode != 31 && entry.Stage != shader::EShaderStage::Pixel) ||
						(mode != 7 && mode != 9 && mode != 12 && mode != 14 && mode != 15 && mode != 31))
					{
						return R::Failure(unsupportedSpirv());
					}

					if (mode == 12)
					{
						if (m_bDepthReplacing)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_bDepthReplacing = true;
					}

					if (mode == 14 || mode == 15)
					{
						if (m_ConservativeDepthMode != 0)
						{
							return R::Failure(unsupportedSpirv());
						}

						m_ConservativeDepthMode = mode;
					}

					offset += count;
					continue;
				}

				if (count != 6 ||
					(entry.Stage != shader::EShaderStage::Compute && entry.Stage != shader::EShaderStage::Mesh && entry.Stage != shader::EShaderStage::Task) ||
					m_bWorkgroupVerified)
				{
					return R::Failure(unsupportedSpirv());
				}

				for (uint32 axis = 0; axis < 3; ++axis)
				{
					if (word(offset + 3 + axis) != entry.ThreadGroupSize[axis])
					{
						return R::Failure(unsupportedSpirv());
					}
				}

				m_bWorkgroupVerified = true;
			}
			else if (op == 331)
			{
				if (count != 6 || word(offset + 1) != m_EntryID || word(offset + 2) != 38 || entry.Stage != shader::EShaderStage::Compute || m_bWorkgroupVerified)
				{
					return R::Failure(unsupportedSpirv());
				}

				for (uint32 axis = 0; axis < 3; ++axis)
				{
					m_WorkgroupIDs[axis] = word(offset + 3 + axis);
					if (m_WorkgroupIDs[axis] == 0 || m_WorkgroupIDs[axis] >= m_Nodes.Size())
					{
						return R::Failure(unsupportedSpirv());
					}
				}

				m_bWorkgroupVerified = true;
			}
			else if (op == 48 || op == 49 || op == 51 || op == 52 || op == 332)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Native SPIR-V specialization requires traced schema and application."));
			}

			offset += count;
		}

		if (m_Members.Size() > 1)
		{
			Sort(
				m_Members.begin(), m_Members.end(),
				[](const SpirvMemberDecoration& left, const SpirvMemberDecoration& right)
				{
					if (left.Type != right.Type)
					{
						return left.Type < right.Type;
					}

					if (left.Member != right.Member)
					{
						return left.Member < right.Member;
					}

					return left.Kind < right.Kind;
				});
		}

		for (usize index = 1; index < m_Members.Size(); ++index)
		{
			const SpirvMemberDecoration& current = m_Members[index];
			const SpirvMemberDecoration& previous = m_Members[index - 1];
			if ((current.Kind == 1 || current.Kind == 6 || current.Kind == 35) &&
				current.Type == previous.Type &&
				current.Member == previous.Member &&
				current.Kind == previous.Kind)
			{
				return R::Failure(unsupportedSpirv());
			}
		}

		return bFoundEntry &&
				((entry.Stage != shader::EShaderStage::Compute && entry.Stage != shader::EShaderStage::Mesh && entry.Stage != shader::EShaderStage::Task) || m_bWorkgroupVerified)
			? R::Success()
			: R::Failure(unsupportedSpirv());
	}

	Result<void> SpirvMetadataReader::reflectType(shader::ShaderVariant& variant, uint32 type, SpirvDecoration decoration, StringView name, bool bInput, uint32 depth)
	{
		using R = Result<void>;

		if (m_Cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		if (depth > 32 ||
			m_TraversalBudget == 0 ||
			type >= m_Nodes.Size() ||
			m_Nodes[type].Definition == 0 ||
			decoration.bUnsupported ||
			(decoration.bFlat && decoration.bNoPerspective))
		{
			return R::Failure(unsupportedSpirv());
		}

		if (m_Nodes[type].Decoration.bUnsupported || !IsValidUtf8(name))
		{
			return R::Failure(unsupportedSpirv());
		}

		--m_TraversalBudget;
		const uint32 definition = m_Nodes[type].Definition;
		const uint32 count = word(definition) >> 16;
		const uint32 op = word(definition) & 0xffffu;
		if (op == 30)
		{
			for (uint32 member = 0; member + 2 < count; ++member)
			{
				SpirvDecoration child = decoration;
				String memberName(m_Resource);
				uint32 nameOffset = 0;

				for (const SpirvMemberDecoration& record : m_Members)
				{
					if (m_TraversalBudget == 0)
					{
						return R::Failure(unsupportedSpirv());
					}

					--m_TraversalBudget;
					if (record.Type != type || record.Member != member)
					{
						continue;
					}

					const SpirvDecoration& value = record.Decoration;
					if (value.Location != shader::NO_STAGE_LOCATION)
					{
						child.Location = value.Location;
					}

					if (value.Builtin != 0xffffffffu)
					{
						child.Builtin = value.Builtin;
					}

					if (value.bHasComponent)
					{
						child.Component = value.Component;
					}

					child.bPerPrimitive |= value.bPerPrimitive;
					child.bFlat |= value.bFlat;
					child.bNoPerspective |= value.bNoPerspective;
					child.bCentroid |= value.bCentroid;
					child.bSample |= value.bSample;
					child.bUnsupported |= value.bUnsupported;
					if (record.NameOffset != 0)
					{
						nameOffset = record.NameOffset;
					}
				}

				if (child.Location == decoration.Location && child.Builtin == 0xffffffffu)
				{
					return R::Failure(unsupportedSpirv());
				}

				char number[16]{};
				const ToCharsResult converted = ToChars(number, number + sizeof(number), member);
				const StringView suffix = nameOffset != 0 ? text(nameOffset + 3, nameOffset + (word(nameOffset) >> 16))
														: StringView(number, static_cast<usize>(converted.ptr - number));
				if (!memberName.Assign(name) || !memberName.Append(".") || !memberName.Append(suffix))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				Result<void> reflected = reflectType(variant, word(definition + 2 + member), child, memberName.View(), bInput, depth + 1);
				if (!reflected)
				{
					return reflected;
				}
			}

			return R::Success();
		}

		shader::ShaderStageIO io(m_Resource);
		if (((m_Stage == shader::EShaderStage::Mesh && !bInput) || (m_Stage == shader::EShaderStage::Pixel && bInput)))
		{
			io.Rate = decoration.bPerPrimitive ? shader::EShaderStageIORate::PerPrimitive : shader::EShaderStageIORate::PerVertex;
		}

		io.Location = decoration.Location;
		io.Component = decoration.Component;
		io.Interpolation = decoration.bFlat		? shader::EShaderInterpolation::Flat
			: decoration.bNoPerspective			? shader::EShaderInterpolation::NoPerspective
			: decoration.Builtin == 0xffffffffu ? shader::EShaderInterpolation::Linear
												: shader::EShaderInterpolation::Unspecified;
		io.bCentroid = decoration.bCentroid;
		io.bSample = decoration.bSample;
		if (!io.Name.Assign(name))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		if (decoration.Builtin != 0xffffffffu)
		{
			Result<shader::EShaderBuiltin> reflected = builtin(decoration.Builtin);
			if (!reflected)
			{
				return R::Failure(Move(reflected.GetError()));
			}

			io.Builtin = reflected.Value();
			io.Location = shader::NO_STAGE_LOCATION;
			io.Component = 0;
		}

		uint32 scalarDefinition = definition;
		if (op == 28 && count == 4)
		{
			const uint32 lengthID = word(definition + 3);
			const uint32 elementID = word(definition + 2);
			if (lengthID >= m_Nodes.Size() || elementID >= m_Nodes.Size())
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 length = m_Nodes[lengthID].Definition;
			if (length == 0 || (word(length) & 0xffffu) != 43 || (word(length) >> 16) != 4)
			{
				return R::Failure(unsupportedSpirv());
			}

			io.ArrayCount = word(length + 3);
			scalarDefinition = m_Nodes[elementID].Definition;
		}

		if (scalarDefinition == 0)
		{
			return R::Failure(unsupportedSpirv());
		}

		if ((word(scalarDefinition) & 0xffffu) == 23 && (word(scalarDefinition) >> 16) == 4)
		{
			io.VectorWidth = word(scalarDefinition + 3);
			const uint32 scalarID = word(scalarDefinition + 2);
			if (scalarID >= m_Nodes.Size())
			{
				return R::Failure(unsupportedSpirv());
			}

			scalarDefinition = m_Nodes[scalarID].Definition;
		}

		if (scalarDefinition == 0)
		{
			return R::Failure(unsupportedSpirv());
		}

		const uint32 scalarOp = word(scalarDefinition) & 0xffffu;
		const uint32 scalarCount = word(scalarDefinition) >> 16;
		if (scalarOp == 20 && scalarCount == 2)
		{
			io.Scalar = shader::EShaderScalar::Bool;
		}
		else if (scalarOp == 21 && scalarCount == 4)
		{
			io.Scalar = word(scalarDefinition + 3) == 0 ? shader::EShaderScalar::UnsignedInteger : shader::EShaderScalar::SignedInteger;
			io.ScalarWidth = word(scalarDefinition + 2);
		}
		else if (scalarOp == 22 && scalarCount == 3)
		{
			io.Scalar = shader::EShaderScalar::Float;
			io.ScalarWidth = word(scalarDefinition + 2);
		}
		else
		{
			return R::Failure(unsupportedSpirv());
		}

		if (io.ScalarWidth != 32)
		{
			return R::Failure(unsupportedSpirv());
		}

		Array<ShaderStageIO>& table = bInput ? variant.StageInputs : variant.StageOutputs;
		if (table.Size() >= 4096 || !table.Add(Move(io)))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		return R::Success();
	}

	bool SpirvMetadataReader::verifyPushField(const shader::ShaderVariant& variant, uint32 type, const shader::ShaderDataField& field, uint32 depth, bool bRecord)
	{
		if (depth > 32 || m_TraversalBudget == 0 || type >= m_Nodes.Size() || m_Nodes[type].Definition == 0 || m_Cancellation.IsCancellationRequested())
		{
			return false;
		}

		--m_TraversalBudget;
		uint32 definition = m_Nodes[type].Definition;
		uint32 count = word(definition) >> 16;
		uint32 op = word(definition) & 0xffffu;
		uint64 elementBytes = field.ByteSize;
		if (op == 30 && count == 3 && field.ArrayStride != 0)
		{
			bool bZeroOffset = false;

			for (const SpirvMemberDecoration& record : m_Members)
			{
				if (m_TraversalBudget == 0)
				{
					return false;
				}

				--m_TraversalBudget;
				if (record.Type == type && record.Member == 0 && record.Decoration.ByteOffset == 0)
				{
					bZeroOffset = true;
				}
			}

			const uint32 arrayID = word(definition + 2);
			if (!bZeroOffset || arrayID >= m_Nodes.Size() || m_Nodes[arrayID].Definition == 0)
			{
				return false;
			}

			type = arrayID;
			definition = m_Nodes[type].Definition;
			count = word(definition) >> 16;
			op = word(definition) & 0xffffu;
		}

		if (op == 28 && count == 4)
		{
			const uint32 lengthID = word(definition + 3);
			const uint32 elementID = word(definition + 2);
			if (lengthID >= m_Nodes.Size() || elementID >= m_Nodes.Size())
			{
				return false;
			}

			const uint32 length = m_Nodes[lengthID].Definition;
			if (length == 0 ||
				(word(length) & 0xffffu) != 43 ||
				(word(length) >> 16) != 4 ||
				word(length + 3) != field.ArrayCount ||
				field.ArrayStride == 0 ||
				field.ArrayStride > 128 ||
				m_Nodes[type].Decoration.ArrayStride != field.ArrayStride ||
				field.ByteSize != field.ArrayStride * field.ArrayCount)
			{
				return false;
			}

			elementBytes = field.ArrayStride;
			definition = m_Nodes[elementID].Definition;
			if (definition == 0)
			{
				return false;
			}

			count = word(definition) >> 16;
			op = word(definition) & 0xffffu;
		}
		else if (field.ArrayCount != 1 || field.ArrayStride != 0)
		{
			return false;
		}

		if (op == 30)
		{
			return field.NestedLayout != shader::NO_DATA_LAYOUT && field.NestedLayout < variant.Layouts.Size() && variant.Layouts[field.NestedLayout].ByteSize == elementBytes &&
				verifyPushLayout(variant, word(definition + 1), field.NestedLayout, depth + 1, bRecord);
		}

		if (field.NestedLayout != shader::NO_DATA_LAYOUT || field.MatrixRows != 1 || field.MatrixColumns != 1 || field.MatrixStride != 0)
		{
			return false;
		}

		uint32 width = 1;
		if (op == 23 && count == 4)
		{
			width = word(definition + 3);
			const uint32 scalarID = word(definition + 2);
			if (scalarID >= m_Nodes.Size() || m_Nodes[scalarID].Definition == 0)
			{
				return false;
			}

			definition = m_Nodes[scalarID].Definition;
			count = word(definition) >> 16;
			op = word(definition) & 0xffffu;
		}

		if (field.VectorWidth != width || field.ScalarWidth != 32 || width == 0 || width > 4 || (field.ArrayStride == 0 ? elementBytes != width * 4 : elementBytes < width * 4))
		{
			return false;
		}

		if (op == 21 && count == 4 && word(definition + 2) == 32 && word(definition + 3) <= 1)
		{
			return field.Scalar == (word(definition + 3) == 0 ? shader::EShaderScalar::UnsignedInteger : shader::EShaderScalar::SignedInteger);
		}

		return op == 22 && count == 3 && word(definition + 2) == 32 && field.Scalar == shader::EShaderScalar::Float;
	}

	bool SpirvMetadataReader::verifyPushLayout(const shader::ShaderVariant& variant, uint32 type, uint32 layoutIndex, uint32 depth, bool bRecord)
	{
		if (depth > 32 || m_TraversalBudget == 0 || type >= m_Nodes.Size() || layoutIndex >= variant.Layouts.Size() || m_Cancellation.IsCancellationRequested())
		{
			return false;
		}

		--m_TraversalBudget;
		const uint32 definition = m_Nodes[type].Definition;
		const ShaderDataLayout& layout = variant.Layouts[layoutIndex];
		if (definition == 0 ||
			(word(definition) & 0xffffu) != 30 ||
			(word(definition) >> 16) != layout.Fields.Size() + 2 ||
			layout.Fields.IsEmpty() ||
			m_Nodes[type].Decoration.bUnsupported ||
			layout.ByteSize > 128 ||
			layout.Alignment == 0 ||
			layout.Alignment > 16 ||
			(layout.Alignment & (layout.Alignment - 1)) != 0)
		{
			return false;
		}

		uint64 end = 0;
		uint32 alignment = 1;

		for (uint32 member = 0; member < layout.Fields.Size(); ++member)
		{
			const ShaderDataField& field = layout.Fields[member];
			uint32 nativeOffset = 0xffffffffu;
			uint32 nameOffset = 0;

			for (const SpirvMemberDecoration& record : m_Members)
			{
				if (m_TraversalBudget == 0)
				{
					return false;
				}

				--m_TraversalBudget;
				if (record.Type == type && record.Member == member)
				{
					if (record.Decoration.bUnsupported)
					{
						return false;
					}

					if (record.Decoration.ByteOffset != 0xffffffffu)
					{
						nativeOffset = record.Decoration.ByteOffset;
					}

					if (record.NameOffset != 0)
					{
						nameOffset = record.NameOffset;
					}
				}
			}

			if (nativeOffset != field.ByteOffset ||
				field.ByteOffset > 128 ||
				field.ByteSize > 128 - field.ByteOffset ||
				nameOffset == 0 ||
				text(nameOffset + 3, nameOffset + (word(nameOffset) >> 16)) != field.Name.View() ||
				!verifyPushField(variant, word(definition + 2 + member), field, depth + 1, bRecord))
			{
				return false;
			}

			const uint32 fieldAlignment = field.NestedLayout != shader::NO_DATA_LAYOUT ? variant.Layouts[field.NestedLayout].Alignment
				: field.VectorWidth == 1											   ? 4u
				: field.VectorWidth == 2											   ? 8u
																					: 16u;
			if (field.ByteOffset % fieldAlignment != 0 || (field.ArrayStride != 0 && field.ArrayStride % fieldAlignment != 0))
			{
				return false;
			}

			alignment = Max(alignment, fieldAlignment);
			end = Max(end, field.ByteOffset + field.ByteSize);
		}

		return (bRecord ? layout.Alignment >= alignment : layout.Alignment == alignment) &&
			layout.ByteSize == ((end + (bRecord ? layout.Alignment : alignment) - 1) & ~uint64((bRecord ? layout.Alignment : alignment) - 1)) &&
			layout.ElementStride == layout.ByteSize;
	}

	Result<void> SpirvMetadataReader::reflectSpecializations(shader::ShaderVariant& variant, shader::ShaderEntryPoint& entry)
	{
		using R = Result<void>;

		uint32 nativeCount = 0;

		for (uint32 id = 1; id < m_Nodes.Size(); ++id)
		{
			if (m_Cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const SpirvMetadataNode& node = m_Nodes[id];
			const uint32 definition = node.Definition;
			if (definition == 0)
			{
				if (node.Decoration.bHasSpecID)
				{
					return R::Failure(unsupportedSpirv());
				}

				continue;
			}

			const uint32 op = word(definition) & 0xffffu;
			if (op == 28 && (word(definition) >> 16) == 4)
			{
				const uint32 lengthID = word(definition + 3);
				if (lengthID >= m_Nodes.Size() || m_Nodes[lengthID].Definition == 0 || (word(m_Nodes[lengthID].Definition) & 0xffffu) != 43)
				{
					return R::Failure(unsupportedSpirv());
				}
			}

			if (op != 50)
			{
				if (node.Decoration.bHasSpecID)
				{
					return R::Failure(unsupportedSpirv());
				}

				continue;
			}

			const bool bRaster = (entry.Stage == shader::EShaderStage::Vertex || entry.Stage == shader::EShaderStage::Pixel);
			if ((entry.Stage != shader::EShaderStage::Compute && !bRaster) ||
				(word(definition) >> 16) != 4 ||
				!node.Decoration.bHasSpecID ||
				node.Decoration.bUnsupported ||
				node.NameOffset == 0 ||
				++nativeCount > 64)
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 typeID = word(definition + 1);
			if (typeID >= m_Nodes.Size() || m_Nodes[typeID].Definition == 0)
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 scalar = m_Nodes[typeID].Definition;
			const uint32 scalarOp = word(scalar) & 0xffffu;
			shader::EShaderScalar kind;
			if (scalarOp == 21 && (word(scalar) >> 16) == 4 && word(scalar + 2) == 32 && word(scalar + 3) <= 1)
			{
				kind = word(scalar + 3) == 0 ? shader::EShaderScalar::UnsignedInteger : shader::EShaderScalar::SignedInteger;
			}
			else if (scalarOp == 22 && (word(scalar) >> 16) == 3 && word(scalar + 2) == 32)
			{
				kind = shader::EShaderScalar::Float;
			}
			else
			{
				return R::Failure(unsupportedSpirv());
			}

			uint32 matches = 0;

			for (const ShaderSpecializationParameter& parameter : variant.SpecializationParameters)
			{
				if (parameter.ID != node.Decoration.SpecID)
				{
					continue;
				}

				++matches;
				if (parameter.Scalar != kind ||
					parameter.ScalarWidth != 32 ||
					!parameter.bHasDefault ||
					parameter.DefaultBits != word(definition + 3) ||
					parameter.Name.View() != text(node.NameOffset + 2, node.NameOffset + (word(node.NameOffset) >> 16)))
				{
					return R::Failure(unsupportedSpirv());
				}
			}

			if (matches != 1)
			{
				return R::Failure(unsupportedSpirv());
			}
		}

		if (nativeCount != variant.SpecializationParameters.Size())
		{
			return R::Failure(unsupportedSpirv());
		}

		for (uint32 axis = 0; axis < 3; ++axis)
		{
			if (m_WorkgroupIDs[axis] == 0)
			{
				continue;
			}

			const uint32 id = m_WorkgroupIDs[axis];
			if (id >= m_Nodes.Size() || m_Nodes[id].Definition == 0)
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 definition = m_Nodes[id].Definition;
			const uint32 op = word(definition) & 0xffffu;
			if ((word(definition) >> 16) != 4 || (op != 43 && op != 50))
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 typeID = word(definition + 1);
			if (typeID >= m_Nodes.Size() || m_Nodes[typeID].Definition == 0)
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 type = m_Nodes[typeID].Definition;
			if ((word(type) & 0xffffu) != 21 || (word(type) >> 16) != 4 || word(type + 2) != 32 || word(type + 3) > 1)
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 size = word(definition + 3);
			if (size == 0 || size > 65535 || (op == 43 && entry.ThreadGroupSize[axis] != size) || (op == 50 && entry.ThreadGroupSize[axis] != 0))
			{
				return R::Failure(unsupportedSpirv());
			}

			if (op == 50)
			{
				for (ShaderSpecializationParameter& parameter : variant.SpecializationParameters)
				{
					if (parameter.ID == m_Nodes[id].Decoration.SpecID)
					{
						if (parameter.Scalar != shader::EShaderScalar::UnsignedInteger || parameter.WorkgroupAxis != shader::NO_STAGE_LOCATION)
						{
							return R::Failure(unsupportedSpirv());
						}

						parameter.WorkgroupAxis = axis;
					}
				}
			}

			entry.ThreadGroupSize[axis] = size;
		}

		return R::Success();
	}

	Result<uint32> SpirvMetadataReader::reflectPayloadTypeRecursive(Array<shader::ShaderPayloadType>& types, uint32 id, uint32 depth)
	{
		using R = Result<uint32>;

		if (m_Cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		if (depth > 16 || id >= m_Nodes.Size() || m_Nodes[id].Definition == 0)
		{
			return R::Failure(unsupportedSpirv());
		}

		for (uint32 index = 0; index < m_PayloadIDCount; ++index)
		{
			if (m_PayloadIDs[index] == id)
			{
				return m_PayloadStates[index] == 2 ? R::Success(m_PayloadTypeIndices[index]) : R::Failure(unsupportedSpirv());
			}
		}

		if (m_PayloadIDCount == 128)
		{
			return R::Failure(unsupportedSpirv());
		}

		const uint32 sourceIndex = m_PayloadIDCount++;
		m_PayloadIDs[sourceIndex] = id;
		m_PayloadStates[sourceIndex] = 1;
		const SpirvDecoration& decoration = m_Nodes[id].Decoration;
		if (decoration.bUnsupported ||
			decoration.bHasArrayStride ||
			decoration.bHasByteOffset ||
			decoration.bHasSpecID ||
			decoration.Location != shader::NO_STAGE_LOCATION ||
			decoration.Component != 0 ||
			decoration.Builtin != 0xffffffffu ||
			decoration.bBlock ||
			decoration.bFlat ||
			decoration.bNoPerspective ||
			decoration.bCentroid ||
			decoration.bSample ||
			decoration.bPerPrimitive)
		{
			return R::Failure(unsupportedSpirv());
		}

		const uint32 definition = m_Nodes[id].Definition;
		const uint32 op = word(definition) & 0xffffu;
		const uint32 count = word(definition) >> 16;
		shader::ShaderPayloadType type(m_Resource);
		if (op == 21 && count == 4 && word(definition + 2) == 32 && word(definition + 3) <= 1)
		{
			type.Scalar = word(definition + 3) == 0 ? shader::EShaderScalar::UnsignedInteger : shader::EShaderScalar::SignedInteger;
			type.ScalarWidth = 32;
		}
		else if (op == 22 && count == 3 && word(definition + 2) == 32)
		{
			type.Scalar = shader::EShaderScalar::Float;
			type.ScalarWidth = 32;
		}
		else if ((op == 23 || op == 28) && count == 4)
		{
			type.Kind = op == 23 ? shader::EShaderPayloadTypeKind::Vector : shader::EShaderPayloadTypeKind::Array;
			if (op == 23)
			{
				type.Count = word(definition + 3);
			}
			else
			{
				const uint32 lengthID = word(definition + 3);
				if (lengthID >= m_Nodes.Size())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 length = m_Nodes[lengthID].Definition;
				if (length == 0 || (word(length) & 0xffffu) != 43 || (word(length) >> 16) != 4)
				{
					return R::Failure(unsupportedSpirv());
				}

				type.Count = word(length + 3);
			}

			if (type.Count == 0 || type.Count > (op == 23 ? 4u : 4096u))
			{
				return R::Failure(unsupportedSpirv());
			}

			Result<uint32> element = reflectPayloadTypeRecursive(types, word(definition + 2), depth + 1);
			if (!element)
			{
				return element;
			}

			type.ElementType = element.Value();
		}
		else if (op == 30 && count > 2 && count <= 34)
		{
			type.Kind = shader::EShaderPayloadTypeKind::Structure;

			for (const SpirvMemberDecoration& member : m_Members)
			{
				if (member.Type == id && member.Kind != 0xffffffffu)
				{
					return R::Failure(unsupportedSpirv());
				}
			}

			for (uint32 index = 2; index < count; ++index)
			{
				Result<uint32> member = reflectPayloadTypeRecursive(types, word(definition + index), depth + 1);
				if (!member)
				{
					return member;
				}

				if (!type.MemberTypes.Add(member.Value()))
				{
					return R::Failure(MakeBuildAllocationError());
				}
			}
		}
		else
		{
			return R::Failure(unsupportedSpirv());
		}

		const uint32 result = static_cast<uint32>(types.Size());
		if (!types.Add(Move(type)))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		m_PayloadStates[sourceIndex] = 2;
		m_PayloadTypeIndices[sourceIndex] = result;

		return R::Success(result);
	}

	Result<void> SpirvMetadataReader::verifyDescriptorCounts(shader::ShaderVariant& variant)
	{
		using R = Result<void>;

		Array<uint32> descriptors(m_Resource);

		for (uint32 id = 0; id < m_Nodes.Size(); ++id)
		{
			if (m_Cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const SpirvMetadataNode& node = m_Nodes[id];
			if (node.Decoration.DescriptorSet == 0xffffffffu && node.Decoration.DescriptorBinding == 0xffffffffu)
			{
				continue;
			}

			if (descriptors.Size() >= 4096 || node.Decoration.DescriptorSet == 0xffffffffu || node.Decoration.DescriptorBinding == 0xffffffffu)
			{
				return R::Failure(unsupportedSpirv());
			}

			if (!descriptors.Add(id))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			const uint32 variable = node.Definition;
			if (variable == 0 || (word(variable) & 0xffffu) != 59 || (word(variable) >> 16) < 4 || word(variable + 1) >= m_Nodes.Size())
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 pointer = m_Nodes[word(variable + 1)].Definition;
			if (pointer == 0 || (word(pointer) & 0xffffu) != 32 || (word(pointer) >> 16) != 4 || word(pointer + 3) >= m_Nodes.Size())
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 type = m_Nodes[word(pointer + 3)].Definition;
			if (type == 0)
			{
				return R::Failure(unsupportedSpirv());
			}

			const bool bRuntime = (word(type) & 0xffffu) == 29;
			uint32 count = 1;
			if ((word(type) & 0xffffu) == 28)
			{
				if ((word(type) >> 16) != 4 || word(type + 3) >= m_Nodes.Size())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 length = m_Nodes[word(type + 3)].Definition;
				if (length == 0 || (word(length) & 0xffffu) != 43 || (word(length) >> 16) != 4 || word(length + 3) == 0)
				{
					return R::Failure(unsupportedSpirv());
				}

				count = word(length + 3);
			}

			uint32 matches = 0;

			for (const ShaderBinding& binding : variant.Bindings)
			{
				if (m_Cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				if (m_TraversalBudget == 0)
				{
					return R::Failure(unsupportedSpirv());
				}

				--m_TraversalBudget;
				if (binding.Scope == shader::EShaderBindingScope::Global && binding.Space == node.Decoration.DescriptorSet && binding.Binding == node.Decoration.DescriptorBinding)
				{
					if (++matches != 1 ||
						(bRuntime ? binding.CountMode != shader::EShaderDescriptorCountMode::Runtime
								: binding.CountMode != shader::EShaderDescriptorCountMode::Fixed || binding.ArrayCount != count))
					{
						return R::Failure(unsupportedSpirv());
					}
				}
			}

			if (matches != 1)
			{
				return R::Failure(unsupportedSpirv());
			}
		}

		for (uint32 bindingIndex = static_cast<uint32>(variant.Bindings.Size()); bindingIndex > 0;)
		{
			--bindingIndex;
			const ShaderBinding& binding = variant.Bindings[bindingIndex];
			if (binding.Scope != shader::EShaderBindingScope::Global)
			{
				continue;
			}

			if (binding.CountMode == shader::EShaderDescriptorCountMode::Fixed)
			{
				uint32 matches = 0;

				for (uint32 const id : descriptors)
				{
					if (m_Cancellation.IsCancellationRequested())
					{
						return R::Failure(MakeBuildCancelledError());
					}

					if (m_TraversalBudget == 0)
					{
						return R::Failure(unsupportedSpirv());
					}

					--m_TraversalBudget;
					const SpirvDecoration& native = m_Nodes[id].Decoration;
					if (native.DescriptorSet == binding.Space && native.DescriptorBinding == binding.Binding)
					{
						++matches;
					}
				}

				if (matches == 0)
				{
					// Preserved logical globals absent from the complete emitted module require no binding.
					RemoveUnusedBinding(variant, bindingIndex);
				}

				continue;
			}

			uint32 matches = 0;
			// Absence must be proven across the final module, not merely the selected interface list.
			for (uint32 const id : descriptors)
			{
				if (m_Cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				if (m_TraversalBudget == 0)
				{
					return R::Failure(unsupportedSpirv());
				}

				--m_TraversalBudget;
				const SpirvMetadataNode& node = m_Nodes[id];
				if (node.Decoration.DescriptorSet != binding.Space || node.Decoration.DescriptorBinding != binding.Binding)
				{
					continue;
				}

				const uint32 variable = node.Definition;
				if (variable == 0 || (word(variable) & 0xffffu) != 59 || (word(variable) >> 16) < 4 || Find(m_Interfaces.begin(), m_Interfaces.end(), id) == m_Interfaces.end())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 pointerID = word(variable + 1);
				if (pointerID >= m_Nodes.Size())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 pointer = m_Nodes[pointerID].Definition;
				if (pointer == 0 || (word(pointer) & 0xffffu) != 32 || (word(pointer) >> 16) != 4)
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 arrayID = word(pointer + 3);
				if (arrayID >= m_Nodes.Size())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 array = m_Nodes[arrayID].Definition;
				if (++matches != 1 || array == 0 || (word(array) & 0xffffu) != 29 || (word(array) >> 16) != 3)
				{
					return R::Failure(unsupportedSpirv());
				}
			}

			if (matches == 0)
			{
				// PreserveParameters can retain another entry's runtime descriptor declaration.
				RemoveUnusedBinding(variant, bindingIndex);
				continue;
			}

			if (matches != 1 || (m_AdditionalFeatures & static_cast<uint64>(shader::EShaderFeature::RuntimeDescriptorArray)) == 0)
			{
				return R::Failure(unsupportedSpirv());
			}
		}

		return R::Success();
	}

	Result<void> SpirvMetadataReader::reflectRay(shader::ShaderVariant& variant)
	{
		using R = Result<void>;

		uint32 recordCount = 0;

		for (uint32 const id : m_Interfaces)
		{
			if (m_Cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			if (id >= m_Nodes.Size())
			{
				return R::Failure(unsupportedSpirv());
			}

			const SpirvMetadataNode& node = m_Nodes[id];
			const uint32 offset = node.Definition;
			if (offset == 0 || (word(offset) & 0xffffu) != 59 || (word(offset) >> 16) < 4)
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 storage = word(offset + 3);
			if (storage == 5343)
			{
				if (++recordCount != 1)
				{
					return R::Failure(unsupportedSpirv());
				}

				const ShaderLocalRecord& record = variant.LocalRecord;
				const uint32 pointerID = word(offset + 1);
				if (record.Lowering != shader::EShaderLocalRecordLowering::VulkanShaderRecord || pointerID >= m_Nodes.Size())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 pointer = m_Nodes[pointerID].Definition;
				if (pointer == 0 ||
					(word(pointer) & 0xffffu) != 32 ||
					(word(pointer) >> 16) != 4 ||
					word(pointer + 2) != storage ||
					word(pointer + 3) >= m_Nodes.Size() ||
					!m_Nodes[word(pointer + 3)].Decoration.bBlock ||
					!verifyPushLayout(variant, word(pointer + 3), record.DataLayout, 1, true))
				{
					return R::Failure(unsupportedSpirv());
				}

				continue;
			}

			if (storage == 9 || storage == 4 || storage == 5343)
			{
				return R::Failure(
					MakeBuildError(
					EErrorCode::Unsupported,
					"Ray push constants, workgroup storage and shader records "
					"require another exact profile."));
			}

			shader::ShaderRayDataSignature data(m_Resource);

			switch (storage)
			{
			case 5338:
				data.Direction = shader::EShaderRayDataDirection::Outgoing;
				break;
			case 5342:
				break;
			case 5339:
				data.Kind = shader::EShaderRayDataKind::HitAttributes;
				data.Direction = m_Stage == shader::EShaderStage::Intersection ? shader::EShaderRayDataDirection::Outgoing : shader::EShaderRayDataDirection::Incoming;
				break;
			case 5328:
				data.Kind = shader::EShaderRayDataKind::Callable;
				data.Direction = shader::EShaderRayDataDirection::Outgoing;
				break;
			case 5329:
				data.Kind = shader::EShaderRayDataKind::Callable;
				break;
			default:
				continue;
			}

			if (variant.RayInterface.Data.Size() >= 64 || node.Decoration.bUnsupported)
			{
				return R::Failure(unsupportedSpirv());
			}

			data.Location = node.Decoration.Location;
			const uint32 pointerID = word(offset + 1);
			if (pointerID >= m_Nodes.Size())
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 pointer = m_Nodes[pointerID].Definition;
			if (pointer == 0 || (word(pointer) & 0xffffu) != 32 || (word(pointer) >> 16) != 4 || word(pointer + 2) != storage)
			{
				return R::Failure(unsupportedSpirv());
			}

			m_PayloadIDCount = 0;
			Result<uint32> root = reflectPayloadTypeRecursive(data.Types, word(pointer + 3), 1);
			if (!root)
			{
				return R::Failure(Move(root.GetError()));
			}

			data.RootType = root.Value();
			if (!variant.RayInterface.Data.Add(Move(data)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		if (recordCount == 0 && variant.LocalRecord.Lowering == shader::EShaderLocalRecordLowering::VulkanShaderRecord)
		{
			// PreserveParameters retains module declarations in Slang reflection. An unused record has no
			// native ABI when the selected final module contains no shader-record storage variable.
			for (uint32 offset = 5; offset < m_WordCount;)
			{
				if (m_Cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				const uint32 instruction = word(offset);
				const uint32 count = instruction >> 16;
				if ((instruction & 0xffffu) == 59 && (count < 4 || word(offset + 3) == 5343))
				{
					return R::Failure(unsupportedSpirv());
				}

				offset += count;
			}

			variant.LocalRecord = {};
		}

		if ((recordCount == 0) != (variant.LocalRecord.Lowering == shader::EShaderLocalRecordLowering::None))
		{
			return R::Failure(unsupportedSpirv());
		}

		variant.RayInterface.bOutgoingDataComplete = true;
		variant.Requirements.Features |= variant.RequiredCapabilities | m_AdditionalFeatures;
		variant.Requirements.MinimumWorkgroupInvocations = 0;
		variant.bStageIOComplete = true;

		return R::Success();
	}

	Result<void> SpirvMetadataReader::Reflect(shader::ShaderVariant& variant, shader::ShaderEntryPoint& entry, [[maybe_unused]] uint32 abi)
	{
		using R = Result<void>;

		m_Stage = entry.Stage;
		m_Code = variant.Bytecode.View();
		if (m_Code.Size() < 20 || m_Code.Size() % 4 != 0 || m_Code.Size() > 16u * 1024u * 1024u)
		{
			return R::Failure(unsupportedSpirv());
		}

		m_WordCount = static_cast<uint32>(m_Code.Size() / 4);
		if (word(0) != 0x07230203 || word(1) != 0x00010600 || word(4) != 0 || word(3) == 0 || word(3) > 262144)
		{
			return R::Failure(unsupportedSpirv());
		}

		if (!m_Nodes.Resize(word(3)))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		Result<void> indexed = indexInstructions(variant, entry);
		if (!indexed)
		{
			return indexed;
		}

		{
			if (entry.Stage == shader::EShaderStage::Mesh)
			{
				if (!m_bTopology || m_MeshOutput.MaximumVertices == 0 || m_MeshOutput.MaximumPrimitives == 0)
				{
					return R::Failure(unsupportedSpirv());
				}

				variant.MeshOutput = m_MeshOutput;
			}

			if (entry.Stage != shader::EShaderStage::Compute && entry.Stage != shader::EShaderStage::Vertex && entry.Stage != shader::EShaderStage::Pixel)
			{
				for (const SpirvMetadataNode& node : m_Nodes)
				{
					if (node.Decoration.bHasSpecID || (node.Definition != 0 && (word(node.Definition) & 0xffffu) == 50))
					{
						return R::Failure(unsupportedSpirv());
					}
				}
			}
		}

		Result<void> specializations = reflectSpecializations(variant, entry);
		if (!specializations)
		{
			return specializations;
		}

		{
			Result<void> counts = verifyDescriptorCounts(variant);
			if (!counts)
			{
				return counts;
			}
		}

		if (entry.Stage >= shader::EShaderStage::RayGeneration)
		{
			return reflectRay(variant);
		}

		uint32 pushCount = 0;
		uint32 payloadID = 0;

		for (uint32 const id : m_Interfaces)
		{
			if (id >= m_Nodes.Size())
			{
				return R::Failure(unsupportedSpirv());
			}

			const SpirvMetadataNode& node = m_Nodes[id];
			const uint32 offset = node.Definition;
			if (offset == 0 || (word(offset) & 0xffffu) != 59 || (word(offset) >> 16) < 4)
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 storage = word(offset + 3);
			if (storage == 5402)
			{
				if (payloadID != 0 || (entry.Stage != shader::EShaderStage::Task && entry.Stage != shader::EShaderStage::Mesh))
				{
					return R::Failure(unsupportedSpirv());
				}

				payloadID = id;
				const uint32 pointerID = word(offset + 1);
				if (pointerID >= m_Nodes.Size())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 pointer = m_Nodes[pointerID].Definition;
				if (pointer == 0 || (word(pointer) & 0xffffu) != 32 || (word(pointer) >> 16) != 4 || word(pointer + 2) != storage)
				{
					return R::Failure(unsupportedSpirv());
				}

				Result<uint32> reflected = reflectPayloadTypeRecursive(variant.TaskPayload.Types, word(pointer + 3), 1);
				if (!reflected)
				{
					return R::Failure(Move(reflected.GetError()));
				}

				variant.TaskPayload.RootType = reflected.Value();
				{
					variant.TaskPayload.Proof = shader::EShaderTaskPayloadProof::Structural;
				}

				continue;
			}

			if (storage == 4 && entry.Stage != shader::EShaderStage::Compute)
			{
				return R::Failure(unsupportedSpirv());
			}

			if (storage == 9)
			{
				const uint32 pointerID = word(offset + 1);
				const bool bVertexRaster = (entry.Stage == shader::EShaderStage::Vertex || entry.Stage == shader::EShaderStage::Pixel);
				const bool bMeshRaster = (entry.Stage == shader::EShaderStage::Mesh || entry.Stage == shader::EShaderStage::Pixel);
				if ((entry.Stage != shader::EShaderStage::Compute && !bVertexRaster && !bMeshRaster) ||
					++pushCount != 1 ||
					variant.PushConstants.Size() != 1 ||
					pointerID >= m_Nodes.Size())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 pointer = m_Nodes[pointerID].Definition;
				if (pointer == 0 || (word(pointer) & 0xffffu) != 32 || (word(pointer) >> 16) != 4 || word(pointer + 2) != 9)
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 blockID = word(pointer + 3);
				if (blockID >= m_Nodes.Size() || !m_Nodes[blockID].Decoration.bBlock || !verifyPushLayout(variant, blockID, variant.PushConstants[0].DataLayout, 0))
				{
					return R::Failure(m_Cancellation.IsCancellationRequested() ? MakeBuildCancelledError() : unsupportedSpirv());
				}

				variant.Requirements.MinimumPushConstantBytes = variant.PushConstants[0].ByteSize;
				continue;
			}

			if (storage != 1 && storage != 3)
			{
				continue;
			}

			if (entry.Stage == shader::EShaderStage::Compute ||
				((entry.Stage == shader::EShaderStage::Task || entry.Stage == shader::EShaderStage::Mesh) && storage == 1 && node.Decoration.Builtin >= 24 &&
					node.Decoration.Builtin <= 29))
			{
				continue;
			}

			const uint32 pointerID = word(offset + 1);
			if (pointerID >= m_Nodes.Size())
			{
				return R::Failure(unsupportedSpirv());
			}

			const uint32 pointer = m_Nodes[pointerID].Definition;
			if (pointer == 0 || (word(pointer) & 0xffffu) != 32 || (word(pointer) >> 16) != 4 || word(pointer + 2) != storage)
			{
				return R::Failure(unsupportedSpirv());
			}

			char number[32]{};
			const ToCharsResult converted = ToChars(number, number + sizeof(number), id);
			String generated(m_Resource);
			StringView name;
			if (node.NameOffset != 0)
			{
				name = text(node.NameOffset + 2, node.NameOffset + (word(node.NameOffset) >> 16));
			}
			else
			{
				if (!generated.Assign(storage == 1 ? StringView("Input") : StringView("Output")) || !generated.Append({number, static_cast<usize>(converted.ptr - number)}))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				name = generated.View();
			}

			uint32 typeID = word(pointer + 3);
			if (entry.Stage == shader::EShaderStage::Mesh && storage == 3)
			{
				if (typeID >= m_Nodes.Size())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 array = m_Nodes[typeID].Definition;
				if (array == 0 || (word(array) & 0xffffu) != 28 || (word(array) >> 16) != 4)
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 lengthID = word(array + 3);
				if (lengthID >= m_Nodes.Size())
				{
					return R::Failure(unsupportedSpirv());
				}

				const uint32 length = m_Nodes[lengthID].Definition;
				const bool bPrimitive = node.Decoration.bPerPrimitive || node.Decoration.Builtin == 5296;
				if (length == 0 ||
					(word(length) & 0xffffu) != 43 ||
					(word(length) >> 16) != 4 ||
					word(length + 3) != (bPrimitive ? m_MeshOutput.MaximumPrimitives : m_MeshOutput.MaximumVertices))
				{
					return R::Failure(unsupportedSpirv());
				}

				typeID = word(array + 2);
				if (node.Decoration.Builtin == 5296)
				{
					if (typeID >= m_Nodes.Size())
					{
						return R::Failure(unsupportedSpirv());
					}

					const uint32 vector = m_Nodes[typeID].Definition;
					if (vector == 0 || (word(vector) & 0xffffu) != 23 || (word(vector) >> 16) != 4 || word(vector + 3) != 3 || word(vector + 2) >= m_Nodes.Size())
					{
						return R::Failure(unsupportedSpirv());
					}

					const uint32 scalar = m_Nodes[word(vector + 2)].Definition;
					if (scalar == 0 || (word(scalar) & 0xffffu) != 21 || (word(scalar) >> 16) != 4 || word(scalar + 2) != 32 || word(scalar + 3) != 0)
					{
						return R::Failure(unsupportedSpirv());
					}

					continue;
				}
			}

			Result<void> reflected = reflectType(variant, typeID, node.Decoration, name, storage == 1, 0);
			if (!reflected)
			{
				return reflected;
			}
		}

		if (pushCount != variant.PushConstants.Size())
		{
			return R::Failure(unsupportedSpirv());
		}

		if (m_ConservativeDepthMode != 0)
		{
			uint32 depthOutputs = 0;

			for (const ShaderStageIO& output : variant.StageOutputs)
			{
				if (output.Builtin == shader::EShaderBuiltin::FragDepth)
				{
					if (output.Scalar != shader::EShaderScalar::Float || output.ScalarWidth != 32 || output.VectorWidth != 1 || output.ArrayCount != 1)
					{
						return R::Failure(unsupportedSpirv());
					}

					++depthOutputs;
				}
			}

			if (!m_bDepthReplacing || depthOutputs != 1)
			{
				return R::Failure(unsupportedSpirv());
			}
		}

		if (entry.Stage == shader::EShaderStage::Task)
		{
			bool bEmit = false;

			for (uint32 offset = 5; offset < m_WordCount; offset += word(offset) >> 16)
			{
				if ((word(offset) & 0xffffu) == 5294)
				{
					const uint32 count = word(offset) >> 16;
					if (count != (payloadID == 0 ? 4u : 5u) || (payloadID != 0 && word(offset + 4) != payloadID))
					{
						return R::Failure(unsupportedSpirv());
					}

					bEmit = true;
				}
			}

			if (!bEmit)
			{
				return R::Failure(unsupportedSpirv());
			}

			m_AdditionalFeatures |= static_cast<uint64>(shader::EShaderFeature::Task);
		}

		variant.Requirements.Features |= variant.RequiredCapabilities | m_AdditionalFeatures;
		uint32 workgroup = 0;
		if (entry.Stage == shader::EShaderStage::Compute || entry.Stage == shader::EShaderStage::Mesh || entry.Stage == shader::EShaderStage::Task)
		{
			workgroup = 1;

			for (const uint32 size : entry.ThreadGroupSize)
			{
				if (size == 0 || size > ((entry.Stage == shader::EShaderStage::Task || entry.Stage == shader::EShaderStage::Mesh) ? 128u : 1024u) / workgroup)
				{
					return R::Failure(unsupportedSpirv());
				}

				workgroup *= size;
			}
		}

		variant.Requirements.MinimumWorkgroupInvocations = workgroup;
		variant.bStageIOComplete = true;

		return R::Success();
	}
} // namespace rox::shader
