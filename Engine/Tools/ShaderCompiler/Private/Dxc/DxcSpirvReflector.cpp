#include "DxcSpirvReflector.h"

#include "../Spirv/SpirvMetadataReader.h"

#include <Algorithms.hpp>
#include <ByteOperations.hpp>
#include <ShaderBuilder.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		constexpr uint32 NONE = 0xffffffffu;
		constexpr uint64 MAXIMUM_BYTES = 64u * 1024u * 1024u;

		Error unsupported()
		{
			return MakeBuildError(EErrorCode::Unsupported, "The final DXC SPIR-V compute interface is outside the bounded native reflection profile.");
		}

		struct Decorations final
		{
			uint32 Set = NONE;
			uint32 Binding = NONE;
			uint32 ArrayStride = 0;
			bool bBlock = false;
			bool bNonWritable = false;
			bool bNonReadable = false;
			bool bUnsupported = false;
		};

		struct Node final
		{
			uint32 Definition = 0;
			uint32 Name = 0;
			uint32 MemberBase = 0;
			uint32 MemberCount = 0;
			uint32 Layouts[2]{NONE, NONE};
			bool bBuilding[2]{};
			Decorations Decoration;
		};

		struct Member final
		{
			uint32 Name = 0;
			uint32 Offset = NONE;
			bool bNonWritable = false;
			bool bNonReadable = false;
			bool bUnsupported = false;
		};

		struct Shape final
		{
			uint64 Bytes = 0;
			uint32 Alignment = 1;
			shader::EShaderScalar Scalar = shader::EShaderScalar::None;
			uint32 VectorWidth = 1;
			uint32 NestedLayout = shader::NO_DATA_LAYOUT;
			uint32 ArrayCount = 1;
			uint64 ArrayStride = 0;
		};

		class NativeInterface final
		{
		public:
			NativeInterface(shader::ShaderVariant& variant, shader::ShaderEntryPoint& entry, MemoryResource& memory, CancellationToken cancellation) noexcept
				: m_Variant(variant)
				, m_Entry(entry)
				, m_Memory(memory)
				, m_Cancellation(cancellation)
				, m_Nodes(memory)
				, m_Members(memory)
			{
			}

			Result<void> Reflect()
			{
				using R = Result<void>;

				if (m_Cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				const Span<Byte> code = m_Variant.Bytecode.View();
				if (m_Entry.Stage != shader::EShaderStage::Compute ||
					m_Variant.Target != CompilerProfile(EShaderSourceLanguage::Hlsl, shader::EShaderTarget::SpirV).Target ||
					code.Size() < 20 ||
					code.Size() % 4 ||
					code.Size() > 16u * 1024u * 1024u ||
					!m_Variant.Bindings.IsEmpty() ||
					!m_Variant.Layouts.IsEmpty() ||
					!m_Variant.ResourceUses.IsEmpty() ||
					!m_Variant.StageInputs.IsEmpty() ||
					!m_Variant.StageOutputs.IsEmpty() ||
					m_Variant.RequiredCapabilities ||
					m_Variant.Requirements != shader::ShaderFeatureRequirements{})
				{
					return R::Failure(unsupported());
				}

				m_WordCount = static_cast<uint32>(code.Size() / 4);
				if (word(0) != 0x07230203 || word(1) != 0x00010600 || word(4) || !word(3) || word(3) > 262144)
				{
					return R::Failure(unsupported());
				}

				if (!m_Nodes.Resize(word(3)))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				Result<void> indexed = index();
				if (!indexed)
				{
					return indexed;
				}

				Result<void> decorated = decorateMembers();
				if (!decorated)
				{
					return decorated;
				}

				Result<void> workgroup = reflectWorkgroup();
				if (!workgroup)
				{
					return workgroup;
				}

				for (uint32 id = 1; id < m_Nodes.Size(); ++id)
				{
					if (m_Cancellation.IsCancellationRequested())
					{
						return R::Failure(MakeBuildCancelledError());
					}

					const uint32 offset = m_Nodes[id].Definition;
					if (!offset || opcode(offset) != 59)
					{
						continue;
					}

					const uint32 storage = word(offset + 3);
					if (storage == 9)
					{
						return R::Failure(unsupported());
					}

					if (storage != 0 && storage != 2 && storage != 12)
					{
						if (m_Nodes[id].Decoration.Set != NONE || m_Nodes[id].Decoration.Binding != NONE)
						{
							return R::Failure(unsupported());
						}

						continue;
					}

					Result<void> binding = reflectBinding(id);
					if (!binding)
					{
						return binding;
					}
				}

				if (m_bAtomic)
				{
					m_Variant.RequiredCapabilities |= static_cast<uint64>(shader::EShaderCapability::Atomic);

					for (ShaderBinding& binding : m_Variant.Bindings)
					{
						if (binding.Kind == shader::EShaderBindingKind::ReadWriteBuffer || binding.Kind == shader::EShaderBindingKind::StorageTexture)
						{
							binding.Access = shader::EShaderResourceAccess::ReadWriteAtomic;
						}
					}
				}

				for (uint32 index = 0; index < m_Variant.Bindings.Size(); ++index)
				{
					if (!m_Variant.ResourceUses.Add({index, m_Variant.Bindings[index].Access}))
					{
						return R::Failure(MakeBuildAllocationError());
					}
				}

				return SpirvMetadataReader(m_Memory, m_Cancellation).Reflect(m_Variant, m_Entry);
			}

		private:
			uint32 word(uint32 offset) const noexcept
			{
				uint32 value;
				CopyBytes(&value, m_Variant.Bytecode.Data() + usize(offset) * 4, 4);

				return value;
			}

			uint32 opcode(uint32 offset) const noexcept
			{
				return word(offset) & 65535u;
			}

			uint32 count(uint32 offset) const noexcept
			{
				return word(offset) >> 16;
			}

			bool validID(uint32 id) const noexcept
			{
				return id != 0 && id < m_Nodes.Size();
			}

			StringView text(uint32 offset, uint32 skip) const noexcept
			{
				if (!offset || skip >= count(offset))
				{
					return {};
				}

				const char* data = reinterpret_cast<const char*>(m_Variant.Bytecode.Data() + usize(offset + skip) * 4);
				const uint32 maximum = (count(offset) - skip) * 4;

				for (uint32 length = 0; length < maximum && length <= 4096; ++length)
				{
					if (!data[length])
					{
						return {data, length};
					}
				}

				return {};
			}

			Result<void> index()
			{
				using R = Result<void>;

				bool bShader = false;
				bool bMemoryModel = false;
				uint32 memberCount = 0;

				for (uint32 offset = 5; offset < m_WordCount;)
				{
					if (m_Cancellation.IsCancellationRequested())
					{
						return R::Failure(MakeBuildCancelledError());
					}

					const uint32 size = count(offset);
					const uint32 op = opcode(offset);
					if (!size || size > m_WordCount - offset)
					{
						return R::Failure(unsupported());
					}

					if ((op == 59 || op == 43) && size < 4)
					{
						return R::Failure(unsupported());
					}

					if ((op >= 19 && op <= 33) || op == 43 || op == 59)
					{
						const uint32 idWord = op == 43 || op == 59 ? 2u : 1u;
						if (size <= idWord || !validID(word(offset + idWord)) || m_Nodes[word(offset + idWord)].Definition)
						{
							return R::Failure(unsupported());
						}

						Node& node = m_Nodes[word(offset + idWord)];
						node.Definition = offset;
						if (op == 30)
						{
							if (size - 2 > 65536u - memberCount)
							{
								return R::Failure(unsupported());
							}

							node.MemberBase = memberCount;
							node.MemberCount = size - 2;
							memberCount += size - 2;
						}
					}

					if (op == 5)
					{
						if (size < 3 || !validID(word(offset + 1)) || m_Nodes[word(offset + 1)].Name || text(offset, 2).IsEmpty())
						{
							return R::Failure(unsupported());
						}

						m_Nodes[word(offset + 1)].Name = offset;
					}
					else if (op == 14)
					{
						if (size != 3 || word(offset + 1) || word(offset + 2) != 1 || bMemoryModel)
						{
							return R::Failure(unsupported());
						}

						bMemoryModel = true;
					}
					else if (op == 15)
					{
						if (size < 5 || word(offset + 1) != 5 || !validID(word(offset + 2)) || m_EntryID || text(offset, 3) != m_Variant.EmittedEntryPoint.View())
						{
							return R::Failure(unsupported());
						}

						m_EntryID = word(offset + 2);
					}
					else if (op == 17)
					{
						if (size != 2)
						{
							return R::Failure(unsupported());
						}

						const uint32 capability = word(offset + 1);
						if (capability != 0 &&
							capability != 1 &&
							capability != 49 &&
							capability != 50 &&
							capability != 55 &&
							capability != 56 &&
							!(capability >= 61 && capability <= 68))
						{
							return R::Failure(unsupported());
						}

						bShader |= capability == 1;
					}
					else if (op == 71)
					{
						if (size < 3 || !validID(word(offset + 1)))
						{
							return R::Failure(unsupported());
						}

						Decorations& decoration = m_Nodes[word(offset + 1)].Decoration;
						const uint32 kind = word(offset + 2);
						if (kind == 1)
						{
							return R::Failure(unsupported());
						}

						if (kind == 2 || kind == 24 || kind == 25)
						{
							if (size != 3)
							{
								return R::Failure(unsupported());
							}

							if (kind == 2)
							{
								decoration.bBlock = true;
							}

							if (kind == 24)
							{
								decoration.bNonWritable = true;
							}

							if (kind == 25)
							{
								decoration.bNonReadable = true;
							}
						}
						else if (kind == 6 || kind == 33 || kind == 34)
						{
							if (size != 4)
							{
								return R::Failure(unsupported());
							}

							uint32& value = kind == 6 ? decoration.ArrayStride : kind == 33 ? decoration.Binding : decoration.Set;
							if (value != (kind == 6 ? 0u : NONE) || word(offset + 3) == NONE || (kind == 6 && !word(offset + 3)))
							{
								return R::Failure(unsupported());
							}

							value = word(offset + 3);
						}
						else if (kind == 11)
						{
							if (size != 4)
							{
								return R::Failure(unsupported());
							}
						}
						else if (kind == 0 || kind == 42)
						{
							if (size != 3)
							{
								return R::Failure(unsupported());
							}
						}
						else
						{
							decoration.bUnsupported = true;
						}
					}
					else if (op == 10 || (op >= 48 && op <= 52) || (op >= 73 && op <= 75) || op == 332)
					{
						return R::Failure(unsupported());
					}
					else if (op == 11 && text(offset, 2) != "GLSL.std.450")
					{
						return R::Failure(unsupported());
					}

					m_bAtomic |= (op >= 227 && op <= 242) || op == 318 || op == 319;
					offset += size;
				}

				if (!bShader || !bMemoryModel || !m_EntryID)
				{
					return R::Failure(unsupported());
				}

				if (!m_Members.Resize(memberCount))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				return R::Success();
			}

			Result<void> decorateMembers()
			{
				using R = Result<void>;

				for (uint32 offset = 5; offset < m_WordCount; offset += count(offset))
				{
					if (m_Cancellation.IsCancellationRequested())
					{
						return R::Failure(MakeBuildCancelledError());
					}

					const uint32 op = opcode(offset);
					if (op != 6 && op != 72)
					{
						continue;
					}

					if (count(offset) < 4 || !validID(word(offset + 1)))
					{
						return R::Failure(unsupported());
					}

					const Node& type = m_Nodes[word(offset + 1)];
					if (!type.Definition || opcode(type.Definition) != 30 || word(offset + 2) >= type.MemberCount)
					{
						return R::Failure(unsupported());
					}

					Member& member = m_Members[type.MemberBase + word(offset + 2)];
					if (op == 6)
					{
						if (member.Name || text(offset, 3).IsEmpty())
						{
							return R::Failure(unsupported());
						}

						member.Name = offset;
						continue;
					}

					const uint32 kind = word(offset + 3);
					if (kind == 35)
					{
						if (count(offset) != 5 || member.Offset != NONE || word(offset + 4) == NONE)
						{
							return R::Failure(unsupported());
						}

						member.Offset = word(offset + 4);
					}
					else if (kind == 24 || kind == 25 || kind == 0)
					{
						if (count(offset) != 4)
						{
							return R::Failure(unsupported());
						}

						member.bNonWritable |= kind == 24;
						member.bNonReadable |= kind == 25;
					}
					else
					{
						member.bUnsupported = true;
					}
				}

				return R::Success();
			}

			Result<uint32> constant(uint32 id) const
			{
				using R = Result<uint32>;

				if (!validID(id))
				{
					return R::Failure(unsupported());
				}

				const uint32 offset = m_Nodes[id].Definition;
				if (!offset || opcode(offset) != 43 || count(offset) != 4 || !validID(word(offset + 1)))
				{
					return R::Failure(unsupported());
				}

				const uint32 type = m_Nodes[word(offset + 1)].Definition;
				if (!type || opcode(type) != 21 || count(type) != 4 || word(type + 2) != 32 || word(type + 3) > 1)
				{
					return R::Failure(unsupported());
				}

				return R::Success(word(offset + 3));
			}

			Result<void> reflectWorkgroup()
			{
				using R = Result<void>;

				bool bFound = false;

				for (uint32 offset = 5; offset < m_WordCount; offset += count(offset))
				{
					if (m_Cancellation.IsCancellationRequested())
					{
						return R::Failure(MakeBuildCancelledError());
					}

					const uint32 op = opcode(offset);
					if (op != 16 && op != 331)
					{
						continue;
					}

					if (count(offset) != 6 || word(offset + 1) != m_EntryID || bFound || word(offset + 2) != (op == 16 ? 17u : 38u))
					{
						return R::Failure(unsupported());
					}

					bFound = true;

					for (uint32 axis = 0; axis < 3; ++axis)
					{
						Result<uint32> size = op == 16 ? Result<uint32>::Success(word(offset + 3 + axis)) : constant(word(offset + 3 + axis));
						if (!size)
						{
							return R::Failure(size.GetError());
						}

						if (!size.Value() || size.Value() > 65535 || (m_Entry.ThreadGroupSize[axis] && m_Entry.ThreadGroupSize[axis] != size.Value()))
						{
							return R::Failure(unsupported());
						}

						m_Entry.ThreadGroupSize[axis] = size.Value();
					}
				}

				return bFound ? R::Success() : R::Failure(unsupported());
			}

			Result<uint32> publish(shader::ShaderDataLayout&& layout)
			{
				using R = Result<uint32>;

				if (m_Variant.Layouts.Size() >= 4096)
				{
					return R::Failure(unsupported());
				}

				Result<Hash256> identity = shader::ComputeShaderLayoutIdentity(layout, m_Memory);
				if (!identity)
				{
					return R::Failure(identity.GetError());
				}

				layout.Identity = identity.Value();
				const uint32 index = static_cast<uint32>(m_Variant.Layouts.Size());
				if (!m_Variant.Layouts.Add(Move(layout)))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				return R::Success(index);
			}

			static void apply(shader::ShaderDataField& field, const Shape& shape)
			{
				field.ByteSize = shape.Bytes;
				field.Scalar = shape.Scalar;
				field.ScalarWidth = shape.Scalar == shader::EShaderScalar::None ? 0u : 32u;
				field.VectorWidth = shape.VectorWidth;
				field.NestedLayout = shape.NestedLayout;
				field.ArrayCount = shape.ArrayCount;
				field.ArrayStride = shape.ArrayStride;
			}

			Result<Shape> shape(uint32 id, bool bConstant, uint32 depth)
			{
				using R = Result<Shape>;

				if (m_Cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				if (!validID(id) || depth > 32 || m_Nodes[id].Decoration.bUnsupported)
				{
					return R::Failure(unsupported());
				}

				const uint32 offset = m_Nodes[id].Definition;
				if (!offset)
				{
					return R::Failure(unsupported());
				}

				const uint32 op = opcode(offset);
				Shape result;
				if (op == 21 || op == 22)
				{
					if (count(offset) != (op == 21 ? 4u : 3u) || word(offset + 2) != 32 || (op == 21 && word(offset + 3) > 1))
					{
						return R::Failure(unsupported());
					}

					result.Bytes = 4;
					result.Alignment = 4;
					result.Scalar = op == 22 ? shader::EShaderScalar::Float : word(offset + 3) ? shader::EShaderScalar::SignedInteger : shader::EShaderScalar::UnsignedInteger;
				}
				else if (op == 23)
				{
					if (count(offset) != 4 || word(offset + 3) < 2 || word(offset + 3) > 4)
					{
						return R::Failure(unsupported());
					}

					Result<Shape> element = shape(word(offset + 2), bConstant, depth + 1);
					if (!element)
					{
						return element;
					}

					if (element.Value().Bytes != 4 || element.Value().Scalar == shader::EShaderScalar::None || element.Value().ArrayStride)
					{
						return R::Failure(unsupported());
					}

					result = element.Value();
					result.VectorWidth = word(offset + 3);
					result.Bytes *= result.VectorWidth;
					result.Alignment = result.VectorWidth == 2 ? 8 : 16;
				}
				else if (op == 28)
				{
					if (count(offset) != 4)
					{
						return R::Failure(unsupported());
					}

					Result<Shape> element = shape(word(offset + 2), bConstant, depth + 1);
					if (!element)
					{
						return element;
					}

					Result<uint32> length = constant(word(offset + 3));
					if (!length)
					{
						return R::Failure(length.GetError());
					}

					const uint32 stride = m_Nodes[id].Decoration.ArrayStride;
					if (!length.Value() || length.Value() > 65536 || element.Value().ArrayStride)
					{
						return R::Failure(unsupported());
					}

					result = element.Value();
					result.Alignment = bConstant ? Max(result.Alignment, 16u) : result.Alignment;
					if (stride < result.Bytes || stride % result.Alignment)
					{
						return R::Failure(unsupported());
					}

					result.ArrayCount = length.Value();
					result.ArrayStride = stride;
					result.Bytes += uint64(length.Value() - 1) * stride;
				}
				else if (op == 30)
				{
					Result<uint32> layout = structure(id, bConstant, depth);
					if (!layout)
					{
						return R::Failure(layout.GetError());
					}

					result.NestedLayout = layout.Value();
					result.Bytes = m_Variant.Layouts[layout.Value()].ByteSize;
					result.Alignment = m_Variant.Layouts[layout.Value()].Alignment;
				}
				else
				{
					return R::Failure(unsupported());
				}

				if (!result.Bytes || result.Bytes > MAXIMUM_BYTES)
				{
					return R::Failure(unsupported());
				}

				return R::Success(result);
			}

			Result<uint32> structure(uint32 id, bool bConstant, uint32 depth)
			{
				using R = Result<uint32>;

				if (depth > 32 || !validID(id))
				{
					return R::Failure(unsupported());
				}

				Node& node = m_Nodes[id];
				const uint32 cache = bConstant ? 1u : 0u;
				if (node.Layouts[cache] != NONE)
				{
					return R::Success(node.Layouts[cache]);
				}

				if (!node.Definition || opcode(node.Definition) != 30 || !node.MemberCount || node.Decoration.bUnsupported || node.bBuilding[cache])
				{
					return R::Failure(unsupported());
				}

				node.bBuilding[cache] = true;
				shader::ShaderDataLayout layout(m_Memory);
				layout.Alignment = bConstant ? 16 : 1;
				uint64 extent = 0;

				for (uint32 index = 0; index < node.MemberCount; ++index)
				{
					const Member& member = m_Members[node.MemberBase + index];
					if (++m_FieldCount > 65536 || member.bUnsupported || member.Offset == NONE || !member.Name)
					{
						return R::Failure(unsupported());
					}

					Result<Shape> value = shape(word(node.Definition + 2 + index), bConstant, depth + 1);
					if (!value)
					{
						return R::Failure(value.GetError());
					}

					if (member.Offset < extent || member.Offset % value.Value().Alignment || member.Offset > MAXIMUM_BYTES - value.Value().Bytes)
					{
						return R::Failure(unsupported());
					}

					shader::ShaderDataField field(m_Memory);
					if (!field.Name.Assign(text(member.Name, 3)))
					{
						return R::Failure(MakeBuildAllocationError());
					}

					field.ByteOffset = member.Offset;
					apply(field, value.Value());
					extent = member.Offset + value.Value().Bytes;
					layout.Alignment = Max(layout.Alignment, value.Value().Alignment);
					if (!layout.Fields.Add(Move(field)))
					{
						return R::Failure(MakeBuildAllocationError());
					}
				}

				layout.ByteSize = (extent + layout.Alignment - 1) / layout.Alignment * layout.Alignment;
				layout.ElementStride = layout.ByteSize;
				if (layout.ByteSize > MAXIMUM_BYTES)
				{
					return R::Failure(unsupported());
				}

				Result<uint32> published = publish(Move(layout));
				if (!published)
				{
					return published;
				}

				node.Layouts[cache] = published.Value();
				node.bBuilding[cache] = false;

				return published;
			}

			Result<void> reflectBinding(uint32 id)
			{
				using R = Result<void>;

				const Node& node = m_Nodes[id];
				const uint32 variable = node.Definition;
				const Decorations& decoration = node.Decoration;
				if (count(variable) != 4 ||
					decoration.bUnsupported ||
					decoration.Set == NONE ||
					decoration.Binding == NONE ||
					!node.Name ||
					m_Variant.Bindings.Size() >= 4096 ||
					!validID(word(variable + 1)))
				{
					return R::Failure(unsupported());
				}

				if (m_Nodes[word(variable + 1)].Decoration.bUnsupported)
				{
					return R::Failure(unsupported());
				}

				const uint32 pointer = m_Nodes[word(variable + 1)].Definition;
				if (!pointer || opcode(pointer) != 32 || count(pointer) != 4 || word(pointer + 2) != word(variable + 3) || !validID(word(pointer + 3)))
				{
					return R::Failure(unsupported());
				}

				const uint32 typeID = word(pointer + 3);
				const Node& type = m_Nodes[typeID];
				if (!type.Definition || type.Decoration.bUnsupported)
				{
					return R::Failure(unsupported());
				}

				shader::ShaderBinding binding(m_Memory);
				if (!binding.Name.Assign(text(node.Name, 2)))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				binding.RegisterNamespace = shader::EShaderRegisterClass::Unified;
				binding.Space = decoration.Set;
				binding.Binding = decoration.Binding;

				for (const ShaderBinding& existing : m_Variant.Bindings)
				{
					if ((existing.Space == binding.Space && existing.Binding == binding.Binding) || existing.Name == binding.Name)
					{
						return R::Failure(unsupported());
					}
				}

				const uint32 storage = word(variable + 3);
				if (storage == 2)
				{
					if (opcode(type.Definition) != 30 || !type.Decoration.bBlock || decoration.bNonReadable)
					{
						return R::Failure(unsupported());
					}

					Result<uint32> layout = structure(typeID, true, 0);
					if (!layout)
					{
						return R::Failure(layout.GetError());
					}

					binding.Kind = shader::EShaderBindingKind::ConstantBuffer;
					binding.DataLayout = layout.Value();
				}
				else if (storage == 12)
				{
					if (opcode(type.Definition) != 30 || !type.Decoration.bBlock || type.MemberCount != 1)
					{
						return R::Failure(unsupported());
					}

					const Member& member = m_Members[type.MemberBase];
					const uint32 arrayID = word(type.Definition + 2);
					if (!validID(arrayID) || member.Offset != 0 || member.bUnsupported)
					{
						return R::Failure(unsupported());
					}

					const Node& array = m_Nodes[arrayID];
					if (!array.Definition || opcode(array.Definition) != 29 || count(array.Definition) != 3 || array.Decoration.bUnsupported)
					{
						return R::Failure(unsupported());
					}

					Result<Shape> element = shape(word(array.Definition + 2), false, 0);
					if (!element)
					{
						return R::Failure(element.GetError());
					}

					const uint32 stride = array.Decoration.ArrayStride;
					if (stride < element.Value().Bytes || stride > MAXIMUM_BYTES || stride % element.Value().Alignment)
					{
						return R::Failure(unsupported());
					}

					shader::ShaderDataLayout layout(m_Memory);
					layout.ByteSize = stride;
					layout.ElementStride = stride;
					layout.Alignment = element.Value().Alignment;
					shader::ShaderDataField field(m_Memory);
					if (++m_FieldCount > 65536)
					{
						return R::Failure(unsupported());
					}

					if (!field.Name.Assign("$element"))
					{
						return R::Failure(MakeBuildAllocationError());
					}

					apply(field, element.Value());
					if (!layout.Fields.Add(Move(field)))
					{
						return R::Failure(MakeBuildAllocationError());
					}

					Result<uint32> published = publish(Move(layout));
					if (!published)
					{
						return R::Failure(published.GetError());
					}

					binding.DataLayout = published.Value();
					const bool bReadOnly = decoration.bNonWritable || type.Decoration.bNonWritable || member.bNonWritable || array.Decoration.bNonWritable;
					const bool bWriteOnly = decoration.bNonReadable || type.Decoration.bNonReadable || member.bNonReadable || array.Decoration.bNonReadable;
					if (bReadOnly && bWriteOnly)
					{
						return R::Failure(unsupported());
					}

					binding.Kind = bReadOnly ? shader::EShaderBindingKind::ReadOnlyBuffer : shader::EShaderBindingKind::ReadWriteBuffer;
					binding.Access = bReadOnly ? shader::EShaderResourceAccess::Read : bWriteOnly ? shader::EShaderResourceAccess::Write : shader::EShaderResourceAccess::ReadWrite;
				}
				else if (opcode(type.Definition) == 26 && count(type.Definition) == 2)
				{
					binding.Kind = shader::EShaderBindingKind::Sampler;
				}
				else if (opcode(type.Definition) == 25)
				{
					const uint32 image = type.Definition;
					if (count(image) != 9 ||
						word(image + 3) != 1 ||
						word(image + 4) == 1 ||
						word(image + 4) > 2 ||
						word(image + 5) ||
						word(image + 6) ||
						(word(image + 7) != 1 && word(image + 7) != 2))
					{
						return R::Failure(unsupported());
					}

					Result<Shape> scalar = shape(word(image + 2), false, 0);
					if (!scalar)
					{
						return R::Failure(scalar.GetError());
					}

					if (scalar.Value().Bytes != 4 || scalar.Value().Scalar == shader::EShaderScalar::None || scalar.Value().ArrayStride)
					{
						return R::Failure(unsupported());
					}

					const bool bStorage = word(image + 7) == 2;
					binding.Kind = bStorage ? shader::EShaderBindingKind::StorageTexture : shader::EShaderBindingKind::SampledTexture;
					binding.TextureShape = shader::EShaderTextureShape::Texture2D;
					const bool bReadOnly = decoration.bNonWritable || type.Decoration.bNonWritable;
					const bool bWriteOnly = decoration.bNonReadable || type.Decoration.bNonReadable;
					if ((bReadOnly && bWriteOnly) || (!bStorage && bWriteOnly))
					{
						return R::Failure(unsupported());
					}

					binding.Access = !bStorage || bReadOnly ? shader::EShaderResourceAccess::Read
						: bWriteOnly						? shader::EShaderResourceAccess::Write
															: shader::EShaderResourceAccess::ReadWrite;
				}
				else
				{
					return R::Failure(unsupported());
				}

				if (!m_Variant.Bindings.Add(Move(binding)))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				return R::Success();
			}

		private:
			shader::ShaderVariant& m_Variant;
			shader::ShaderEntryPoint& m_Entry;
			MemoryResource& m_Memory;
			CancellationToken m_Cancellation;
			Array<Node> m_Nodes;
			Array<Member> m_Members;
			uint32 m_WordCount = 0;
			uint32 m_EntryID = 0;
			uint32 m_FieldCount = 0;
			bool m_bAtomic = false;
		};

	} // namespace

	Result<void> reflectDxcSpirv(shader::ShaderVariant& variant, shader::ShaderEntryPoint& entry, MemoryResource& memory, CancellationToken cancellation)
	{
		return NativeInterface(variant, entry, memory, cancellation).Reflect();
	}
} // namespace rox::shader
