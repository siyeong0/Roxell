#include "DxcReflector.h"

#include <Cancellation.h>
#include <Comparison.hpp>
#include <StringView.hpp>
#include <Utility.hpp>

#if defined(_WIN32)
namespace rox::shader
{
	using namespace core;

	namespace
	{
		Error unsupportedReflection()
		{
			return MakeBuildError(EErrorCode::Unsupported, "DXC reflection contains an unsupported or unresolved data or resource category.");
		}

		Error unsupportedRequirements(uint64 flags)
		{
			char message[] = "DXC reflection requires unsupported flags 0x0000000000000000.";
			constexpr uint32 HEX_OFFSET = sizeof("DXC reflection requires unsupported flags 0x") - 1;
			constexpr char DIGITS[] = "0123456789abcdef";

			for (uint32 index = 0; index < 16; ++index)
			{
				message[HEX_OFFSET + index] = DIGITS[(flags >> ((15 - index) * 4)) & 15];
			}

			return MakeBuildError(EErrorCode::Unsupported, {message, sizeof(message) - 1});
		}

	} // namespace

	Result<uint64> ReflectDxilRequirements(ID3D12ShaderReflection& reflection, [[maybe_unused]] uint32 abi)
	{
		using R = Result<uint64>;

		const UINT64 flags = reflection.GetRequiresFlags();
		constexpr UINT64 SUPPORTED_FLAGS = D3D_SHADER_REQUIRES_DOUBLES | D3D_SHADER_REQUIRES_EARLY_DEPTH_STENCIL | D3D_SHADER_REQUIRES_UAVS_AT_EVERY_STAGE |
			D3D_SHADER_REQUIRES_64_UAVS | D3D_SHADER_REQUIRES_11_1_DOUBLE_EXTENSIONS | D3D_SHADER_REQUIRES_INT64_OPS | D3D_SHADER_REQUIRES_NATIVE_16BIT_OPS |
			D3D_SHADER_REQUIRES_WAVE_OPS | D3D_SHADER_REQUIRES_TYPED_UAV_LOAD_ADDITIONAL_FORMATS | D3D_SHADER_REQUIRES_MINIMUM_PRECISION;
		const uint64 allowed = SUPPORTED_FLAGS | (uint64{0x06000000});
		if ((flags & ~allowed) != 0)
		{
			return R::Failure(unsupportedRequirements(flags));
		}

		D3D12_SHADER_DESC desc{};
		if (FAILED(reflection.GetDesc(&desc)))
		{
			return R::Failure(unsupportedReflection());
		}

		uint64 result = 0;
		if ((flags & D3D_SHADER_REQUIRES_DOUBLES) != 0)
		{
			result |= static_cast<uint64>(shader::EShaderCapability::Float64);
		}

		if ((flags & D3D_SHADER_REQUIRES_INT64_OPS) != 0)
		{
			result |= static_cast<uint64>(shader::EShaderCapability::Int64);
		}

		if ((flags & D3D_SHADER_REQUIRES_NATIVE_16BIT_OPS) != 0)
		{
			result |= static_cast<uint64>(shader::EShaderCapability::Float16);
		}

		if (desc.cInterlockedInstructions != 0)
		{
			result |= static_cast<uint64>(shader::EShaderCapability::Atomic);
		}

		return R::Success(result);
	}

	DxcReflector::DxcReflector(shader::ShaderVariant& variant, MemoryResource& resource, CancellationToken cancellation)
		: m_Variant(variant)
		, m_Resource(resource)
		, m_Cancellation(cancellation)
	{
	}

	Result<TypeShape> DxcReflector::reflectShapeRecursive(ID3D12ShaderReflectionType* type, bool bConstant, uint32 depth)
	{
		using R = Result<TypeShape>;

		if (m_Cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		D3D12_SHADER_TYPE_DESC desc{};
		if (type == nullptr || depth > 32 || FAILED(type->GetDesc(&desc)) || desc.Elements > 65536 || desc.Members > 65536)
		{
			return R::Failure(unsupportedReflection());
		}

		TypeShape shape;
		if (desc.Class == D3D_SVC_STRUCT)
		{
			if (desc.Members == 0)
			{
				return R::Failure(unsupportedReflection());
			}

			for (UINT index = 0; index < desc.Members; ++index)
			{
				ID3D12ShaderReflectionType* member = type->GetMemberTypeByIndex(index);
				D3D12_SHADER_TYPE_DESC memberDesc{};
				if (member == nullptr || FAILED(member->GetDesc(&memberDesc)))
				{
					return R::Failure(unsupportedReflection());
				}

				Result<TypeShape> memberShape = reflectShapeRecursive(member, bConstant, depth + 1);
				if (!memberShape)
				{
					return R::Failure(memberShape.GetError());
				}

				shape.Extent = Max(shape.Extent, uint64(memberDesc.Offset) + memberShape.Value().Extent);
				shape.Alignment = Max(shape.Alignment, memberShape.Value().Alignment);
			}

			if (bConstant)
			{
				shape.Alignment = Max(shape.Alignment, 16u);
			}
		}
		else
		{
			switch (desc.Type)
			{
			case D3D_SVT_BOOL:
				shape.Scalar = shader::EShaderScalar::Bool;
				shape.ScalarWidth = 32;
				break;
			case D3D_SVT_INT:
				shape.Scalar = shader::EShaderScalar::SignedInteger;
				shape.ScalarWidth = 32;
				break;
			case D3D_SVT_UINT:
				shape.Scalar = shader::EShaderScalar::UnsignedInteger;
				shape.ScalarWidth = 32;
				break;
			case D3D_SVT_FLOAT:
				shape.Scalar = shader::EShaderScalar::Float;
				shape.ScalarWidth = 32;
				break;
			case D3D_SVT_DOUBLE:
				shape.Scalar = shader::EShaderScalar::Float;
				shape.ScalarWidth = 64;
				break;
			case D3D_SVT_INT16:
				shape.Scalar = shader::EShaderScalar::SignedInteger;
				shape.ScalarWidth = 16;
				break;
			case D3D_SVT_UINT16:
				shape.Scalar = shader::EShaderScalar::UnsignedInteger;
				shape.ScalarWidth = 16;
				break;
			case D3D_SVT_FLOAT16:
				shape.Scalar = shader::EShaderScalar::Float;
				shape.ScalarWidth = 16;
				break;
			case D3D_SVT_INT64:
				shape.Scalar = shader::EShaderScalar::SignedInteger;
				shape.ScalarWidth = 64;
				break;
			case D3D_SVT_UINT64:
				shape.Scalar = shader::EShaderScalar::UnsignedInteger;
				shape.ScalarWidth = 64;
				break;
			default:
				return R::Failure(unsupportedReflection());
			}

			if (desc.Rows == 0 || desc.Rows > 4 || desc.Columns == 0 || desc.Columns > 4)
			{
				return R::Failure(unsupportedReflection());
			}

			shape.Alignment = shape.ScalarWidth / 8;
			if (desc.Class == D3D_SVC_SCALAR || desc.Class == D3D_SVC_VECTOR)
			{
				shape.Extent = uint64(desc.Columns) * shape.Alignment;
			}
			else if (desc.Class == D3D_SVC_MATRIX_ROWS || desc.Class == D3D_SVC_MATRIX_COLUMNS)
			{
				const UINT major = desc.Class == D3D_SVC_MATRIX_ROWS ? desc.Rows : desc.Columns;
				const UINT minor = desc.Class == D3D_SVC_MATRIX_ROWS ? desc.Columns : desc.Rows;
				const uint64 vectorBytes = uint64(minor) * shape.Alignment;
				shape.MatrixStride = bConstant ? (vectorBytes + 15) / 16 * 16 : vectorBytes;
				shape.Extent = shape.MatrixStride * (major - 1) + vectorBytes;
				if (bConstant)
				{
					shape.Alignment = Max(shape.Alignment, 16u);
				}
			}
			else
			{
				return R::Failure(unsupportedReflection());
			}
		}

		shape.ElementExtent = shape.Extent;
		if (desc.Elements != 0)
		{
			if (bConstant)
			{
				shape.Alignment = Max(shape.Alignment, 16u);
			}

			shape.ArrayStride = (shape.Extent + shape.Alignment - 1) / shape.Alignment * shape.Alignment;
			shape.Extent = shape.ArrayStride * (desc.Elements - 1) + shape.ElementExtent;
		}

		if (shape.Extent == 0 || shape.Extent > 64u * 1024u * 1024u)
		{
			return R::Failure(unsupportedReflection());
		}

		return R::Success(shape);
	}

	Result<void> DxcReflector::reflectFieldRecursive(ID3D12ShaderReflectionType* type, shader::ShaderDataField& field, uint64 reflectedSize, bool bConstant, uint32 depth)
	{
		using R = Result<void>;

		D3D12_SHADER_TYPE_DESC desc{};
		if (type == nullptr || FAILED(type->GetDesc(&desc)))
		{
			return R::Failure(unsupportedReflection());
		}

		Result<TypeShape> shape = reflectShapeRecursive(type, bConstant, depth);
		if (!shape)
		{
			return R::Failure(shape.GetError());
		}

		if (reflectedSize != 0 && shape.Value().Extent > reflectedSize)
		{
			return R::Failure(unsupportedReflection());
		}

		field.ByteSize = reflectedSize == 0 ? shape.Value().Extent : reflectedSize;
		field.ArrayCount = desc.Elements == 0 ? 1 : desc.Elements;
		field.ArrayStride = shape.Value().ArrayStride;
		if (desc.Class == D3D_SVC_STRUCT)
		{
			Result<uint32> nested = reflectStructLayoutRecursive(type, shape.Value().ElementExtent, bConstant, depth + 1);
			if (!nested)
			{
				return R::Failure(nested.GetError());
			}

			field.NestedLayout = nested.Value();
		}
		else
		{
			field.Scalar = shape.Value().Scalar;
			field.ScalarWidth = shape.Value().ScalarWidth;
			if (desc.Class == D3D_SVC_VECTOR)
			{
				field.VectorWidth = desc.Columns;
			}

			if (desc.Class == D3D_SVC_MATRIX_ROWS || desc.Class == D3D_SVC_MATRIX_COLUMNS)
			{
				field.MatrixRows = desc.Rows;
				field.MatrixColumns = desc.Columns;
				field.MatrixStride = shape.Value().MatrixStride;
				field.MatrixOrder = desc.Class == D3D_SVC_MATRIX_ROWS ? shader::EShaderMatrixOrder::RowMajor : shader::EShaderMatrixOrder::ColumnMajor;
			}
		}

		return R::Success();
	}

	Result<uint32> DxcReflector::reflectStructLayoutRecursive(ID3D12ShaderReflectionType* type, uint64 extent, bool bConstant, uint32 depth)
	{
		using R = Result<uint32>;

		D3D12_SHADER_TYPE_DESC desc{};
		if (depth > 32 || FAILED(type->GetDesc(&desc)))
		{
			return R::Failure(unsupportedReflection());
		}

		Result<TypeShape> shape = reflectShapeRecursive(type, bConstant, depth);
		if (!shape)
		{
			return R::Failure(shape.GetError());
		}

		shader::ShaderDataLayout layout(m_Resource);
		layout.ByteSize = extent;
		layout.Alignment = shape.Value().Alignment;
		layout.ElementStride = (extent + layout.Alignment - 1) / layout.Alignment * layout.Alignment;

		for (UINT index = 0; index < desc.Members; ++index)
		{
			if (m_FieldCount >= 65536)
			{
				return R::Failure(unsupportedReflection());
			}

			++m_FieldCount;
			ID3D12ShaderReflectionType* member = type->GetMemberTypeByIndex(index);
			const char* name = type->GetMemberTypeName(index);
			D3D12_SHADER_TYPE_DESC memberDesc{};
			if (member == nullptr || name == nullptr || FAILED(member->GetDesc(&memberDesc)))
			{
				return R::Failure(unsupportedReflection());
			}

			shader::ShaderDataField field(m_Resource);
			field.ByteOffset = memberDesc.Offset;
			if (!field.Name.Assign(StringView(name)))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			Result<void> reflected = reflectFieldRecursive(member, field, 0, bConstant, depth);
			if (!reflected)
			{
				return R::Failure(reflected.GetError());
			}

			if (!layout.Fields.Add(Move(field)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		return publishLayout(Move(layout));
	}

	Result<uint32> DxcReflector::reflectBufferLayout(ID3D12ShaderReflectionConstantBuffer* buffer, bool bConstant)
	{
		using R = Result<uint32>;

		D3D12_SHADER_BUFFER_DESC desc{};
		if (buffer == nullptr ||
			FAILED(buffer->GetDesc(&desc)) ||
			desc.Size == 0 ||
			desc.Size > 64u * 1024u * 1024u ||
			desc.Variables == 0 ||
			desc.Variables > 65536 ||
			desc.Type != (bConstant ? D3D_CT_CBUFFER : D3D_CT_RESOURCE_BIND_INFO))
		{
			return R::Failure(unsupportedReflection());
		}

		shader::ShaderDataLayout layout(m_Resource);
		layout.ByteSize = desc.Size;
		layout.Alignment = bConstant ? 16 : 1;

		for (UINT index = 0; index < desc.Variables; ++index)
		{
			if (m_FieldCount >= 65536)
			{
				return R::Failure(unsupportedReflection());
			}

			++m_FieldCount;
			ID3D12ShaderReflectionVariable* variable = buffer->GetVariableByIndex(index);
			D3D12_SHADER_VARIABLE_DESC variableDesc{};
			if (variable == nullptr || FAILED(variable->GetDesc(&variableDesc)) || variableDesc.Name == nullptr)
			{
				return R::Failure(unsupportedReflection());
			}

			shader::ShaderDataField field(m_Resource);
			field.ByteOffset = variableDesc.StartOffset;
			const StringView reflectedName(variableDesc.Name);
			const StringView name = !bConstant && reflectedName == "$Element" ? StringView("$element") : reflectedName;
			if (!field.Name.Assign(name))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			Result<void> reflected = reflectFieldRecursive(variable->GetType(), field, variableDesc.Size, bConstant, 0);
			if (!reflected)
			{
				return R::Failure(reflected.GetError());
			}

			Result<TypeShape> shape = reflectShapeRecursive(variable->GetType(), bConstant, 0);
			if (!shape)
			{
				return R::Failure(shape.GetError());
			}

			layout.Alignment = Max(layout.Alignment, shape.Value().Alignment);
			if (!layout.Fields.Add(Move(field)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		layout.ElementStride = (layout.ByteSize + layout.Alignment - 1) / layout.Alignment * layout.Alignment;

		return publishLayout(Move(layout));
	}

	Result<void> DxcReflector::Reflect(ID3D12ShaderReflection& reflection, shader::ShaderEntryPoint& entry)
	{
		using R = Result<void>;

		D3D12_SHADER_DESC desc{};
		if (FAILED(reflection.GetDesc(&desc)) || desc.BoundResources > 4096)
		{
			return R::Failure(unsupportedReflection());
		}

		const UINT expectedStage = entry.Stage == shader::EShaderStage::Compute ? D3D12_SHVER_COMPUTE_SHADER
			: entry.Stage == shader::EShaderStage::Vertex						? D3D12_SHVER_VERTEX_SHADER
																				: D3D12_SHVER_PIXEL_SHADER;
		if (D3D12_SHVER_GET_TYPE(desc.Version) != expectedStage || D3D12_SHVER_GET_MAJOR(desc.Version) != 6 || D3D12_SHVER_GET_MINOR(desc.Version) != 6)
		{
			return R::Failure(unsupportedReflection());
		}

		if (entry.Stage == shader::EShaderStage::Compute && reflection.GetThreadGroupSize(&entry.ThreadGroupSize[0], &entry.ThreadGroupSize[1], &entry.ThreadGroupSize[2]) == 0)
		{
			return R::Failure(unsupportedReflection());
		}

		Result<uint64> required = ReflectDxilRequirements(reflection);
		if (!required)
		{
			return R::Failure(required.GetError());
		}

		m_Variant.RequiredCapabilities |= required.Value();

		for (UINT index = 0; index < desc.BoundResources; ++index)
		{
			if (m_Cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			D3D12_SHADER_INPUT_BIND_DESC resource{};
			if (FAILED(reflection.GetResourceBindingDesc(index, &resource)) || resource.Name == nullptr || resource.BindCount == 0 || resource.BindCount > 65536)
			{
				return R::Failure(unsupportedReflection());
			}

			shader::ShaderBinding binding(m_Resource);
			binding.Binding = resource.BindPoint;
			binding.Space = resource.Space;
			binding.ArrayCount = resource.BindCount;
			if (!binding.Name.Assign(StringView(resource.Name)))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			bool bConstant = false;
			bool bStructured = false;

			switch (resource.Type)
			{
			case D3D_SIT_CBUFFER:
				binding.Kind = shader::EShaderBindingKind::ConstantBuffer;
				binding.RegisterNamespace = shader::EShaderRegisterClass::ConstantBuffer;
				bConstant = true;
				break;
			case D3D_SIT_SAMPLER:
				binding.Kind = shader::EShaderBindingKind::Sampler;
				binding.RegisterNamespace = shader::EShaderRegisterClass::Sampler;
				break;
			case D3D_SIT_STRUCTURED:
				bStructured = true;
				[[fallthrough]];
			case D3D_SIT_BYTEADDRESS:
				binding.Kind = shader::EShaderBindingKind::ReadOnlyBuffer;
				binding.RegisterNamespace = shader::EShaderRegisterClass::ShaderResource;
				break;
			case D3D_SIT_UAV_RWSTRUCTURED:
				bStructured = true;
				[[fallthrough]];
			case D3D_SIT_UAV_RWBYTEADDRESS:
				binding.Kind = shader::EShaderBindingKind::ReadWriteBuffer;
				binding.RegisterNamespace = shader::EShaderRegisterClass::UnorderedAccess;
				binding.Access = shader::EShaderResourceAccess::ReadWrite;
				break;
			case D3D_SIT_TEXTURE:
				binding.Kind = shader::EShaderBindingKind::SampledTexture;
				binding.RegisterNamespace = shader::EShaderRegisterClass::ShaderResource;
				break;
			case D3D_SIT_UAV_RWTYPED:
				binding.Kind = shader::EShaderBindingKind::StorageTexture;
				binding.RegisterNamespace = shader::EShaderRegisterClass::UnorderedAccess;
				binding.Access = shader::EShaderResourceAccess::ReadWrite;
				break;
			default:
				return R::Failure(unsupportedReflection());
			}

			if (binding.Kind == shader::EShaderBindingKind::SampledTexture || binding.Kind == shader::EShaderBindingKind::StorageTexture)
			{
				switch (resource.Dimension)
				{
				case D3D_SRV_DIMENSION_BUFFER:
					binding.TextureShape = shader::EShaderTextureShape::Buffer;
					break;
				case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
					binding.bTextureArray = true;
					[[fallthrough]];
				case D3D_SRV_DIMENSION_TEXTURE1D:
					binding.TextureShape = shader::EShaderTextureShape::Texture1D;
					break;
				case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
					binding.bTextureArray = true;
					[[fallthrough]];
				case D3D_SRV_DIMENSION_TEXTURE2DMS:
					binding.bMultisampled = true;
					binding.TextureShape = shader::EShaderTextureShape::Texture2D;
					break;
				case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
					binding.bTextureArray = true;
					[[fallthrough]];
				case D3D_SRV_DIMENSION_TEXTURE2D:
					binding.TextureShape = shader::EShaderTextureShape::Texture2D;
					break;
				case D3D_SRV_DIMENSION_TEXTURE3D:
					binding.TextureShape = shader::EShaderTextureShape::Texture3D;
					break;
				case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
					binding.bTextureArray = true;
					[[fallthrough]];
				case D3D_SRV_DIMENSION_TEXTURECUBE:
					binding.TextureShape = shader::EShaderTextureShape::Cube;
					break;
				default:
					return R::Failure(unsupportedReflection());
				}
			}

			if (bConstant || bStructured)
			{
				Result<uint32> layout = reflectBufferLayout(reflection.GetConstantBufferByName(resource.Name), bConstant);
				if (!layout)
				{
					return R::Failure(layout.GetError());
				}

				binding.DataLayout = layout.Value();
			}

			if (desc.cInterlockedInstructions != 0 && binding.RegisterNamespace == shader::EShaderRegisterClass::UnorderedAccess)
			{
				binding.Access = shader::EShaderResourceAccess::ReadWriteAtomic;
			}

			shader::ShaderResourceUse use{static_cast<uint32>(m_Variant.Bindings.Size()), binding.Access};
			if (!m_Variant.Bindings.Add(Move(binding)) || !m_Variant.ResourceUses.Add(use))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		return R::Success();
	}

	Result<uint32> DxcReflector::publishLayout(shader::ShaderDataLayout&& layout)
	{
		using R = Result<uint32>;

		if (m_Variant.Layouts.Size() >= 4096)
		{
			return R::Failure(unsupportedReflection());
		}

		Result<Hash256> identity = shader::ComputeShaderLayoutIdentity(layout, m_Resource);
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
} // namespace rox::shader
#endif
