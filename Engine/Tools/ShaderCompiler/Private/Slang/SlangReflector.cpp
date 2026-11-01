#include "SlangReflector.h"

#include <ByteReader.h>
#include <Cancellation.h>
#include <StringView.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>

#include <slang-com-ptr.h>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		Result<void> reflectionFailure()
		{
			return Result<void>::Failure(MakeBuildError(EErrorCode::Unsupported, "Compiler reflection contains an unsupported or unresolved layout category."));
		}

	} // namespace

	SlangReflector::SlangReflector(
		shader::ShaderVariant& variant,
		MemoryResource& resource,
		slang::ISession& session,
		SlangInt target,
		CancellationToken cancellation,
		[[maybe_unused]] uint32 abi)
		: m_Variant(variant)
		, m_Resource(resource)
		, m_Session(session)
		, m_Target(target)
		, m_Cancellation(cancellation)
	{
	}

	Result<void> SlangReflector::reflectFieldRecursive(slang::TypeLayoutReflection* type, shader::ShaderDataField& field, uint32 depth)
	{
		using Kind = slang::TypeReflection::Kind;
		using Scalar = slang::TypeReflection;
		if (m_Cancellation.IsCancellationRequested())
		{
			return Result<void>::Failure(MakeBuildCancelledError());
		}

		if (type == nullptr || depth > 32)
		{
			return reflectionFailure();
		}

		field.ByteSize = type->getSize();
		if (field.ByteSize == 0 || field.ByteSize > 64u * 1024u * 1024u)
		{
			return reflectionFailure();
		}

		if (type->getKind() == Kind::Array)
		{
			const size_t count = type->getElementCount();
			if (count == 0 || count > 65536)
			{
				return reflectionFailure();
			}

			field.ArrayCount = static_cast<uint32>(count);
			field.ArrayStride = type->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
			type = type->getElementTypeLayout();
			if (type == nullptr)
			{
				return reflectionFailure();
			}
		}

		if (type->getKind() == Kind::Struct || type->getKind() == Kind::Array)
		{
			Result<uint32> nested = reflectLayoutRecursive(type, depth + 1);
			if (!nested)
			{
				return Result<void>::Failure(nested.GetError());
			}

			field.NestedLayout = nested.Value();

			return Result<void>::Success();
		}

		if (type->getKind() != Kind::Scalar && type->getKind() != Kind::Vector && type->getKind() != Kind::Matrix)
		{
			return reflectionFailure();
		}

		switch (type->getScalarType())
		{
		case Scalar::Bool:
			field.Scalar = shader::EShaderScalar::Bool;
			field.ScalarWidth = 32;
			break;
		case Scalar::Int8:
			field.Scalar = shader::EShaderScalar::SignedInteger;
			field.ScalarWidth = 8;
			break;
		case Scalar::Int16:
			field.Scalar = shader::EShaderScalar::SignedInteger;
			field.ScalarWidth = 16;
			break;
		case Scalar::Int32:
			field.Scalar = shader::EShaderScalar::SignedInteger;
			field.ScalarWidth = 32;
			break;
		case Scalar::Int64:
			field.Scalar = shader::EShaderScalar::SignedInteger;
			field.ScalarWidth = 64;
			break;
		case Scalar::UInt8:
			field.Scalar = shader::EShaderScalar::UnsignedInteger;
			field.ScalarWidth = 8;
			break;
		case Scalar::UInt16:
			field.Scalar = shader::EShaderScalar::UnsignedInteger;
			field.ScalarWidth = 16;
			break;
		case Scalar::UInt32:
			field.Scalar = shader::EShaderScalar::UnsignedInteger;
			field.ScalarWidth = 32;
			break;
		case Scalar::UInt64:
			field.Scalar = shader::EShaderScalar::UnsignedInteger;
			field.ScalarWidth = 64;
			break;
		case Scalar::Float16:
			field.Scalar = shader::EShaderScalar::Float;
			field.ScalarWidth = 16;
			break;
		case Scalar::Float32:
			field.Scalar = shader::EShaderScalar::Float;
			field.ScalarWidth = 32;
			break;
		case Scalar::Float64:
			field.Scalar = shader::EShaderScalar::Float;
			field.ScalarWidth = 64;
			break;
		default:
			return reflectionFailure();
		}

		if (field.ScalarWidth == 64)
		{
			m_Variant.RequiredCapabilities |= static_cast<uint64>(
				field.Scalar == shader::EShaderScalar::Float ? shader::EShaderCapability::Float64 : shader::EShaderCapability::Int64);
		}

		if (field.ScalarWidth == 16 && field.Scalar == shader::EShaderScalar::Float)
		{
			m_Variant.RequiredCapabilities |= static_cast<uint64>(shader::EShaderCapability::Float16);
		}

		if (type->getKind() == Kind::Vector)
		{
			field.VectorWidth = type->getColumnCount();
		}

		if (type->getKind() == Kind::Matrix)
		{
			field.MatrixRows = type->getRowCount();
			field.MatrixColumns = type->getColumnCount();
			field.MatrixOrder = type->getMatrixLayoutMode() == SLANG_MATRIX_LAYOUT_ROW_MAJOR ? shader::EShaderMatrixOrder::RowMajor : shader::EShaderMatrixOrder::ColumnMajor;
			// The pinned SDK exposes array stride but not matrix major-vector stride.
			// Its matrix layout aligns each reflected major vector to the matrix alignment.
			const uint32 minor = field.MatrixOrder == shader::EShaderMatrixOrder::RowMajor ? field.MatrixColumns : field.MatrixRows;
			const int32_t alignment = type->getAlignment();
			if (alignment <= 0 || (alignment & (alignment - 1)) != 0)
			{
				return reflectionFailure();
			}

			field.MatrixStride = (uint64(minor) * (field.ScalarWidth / 8) + alignment - 1) & ~uint64(alignment - 1);
		}

		return Result<void>::Success();
	}

	Result<uint32> SlangReflector::reflectLayoutRecursive(slang::TypeLayoutReflection* type, uint32 depth)
	{
		using R = Result<uint32>;

		if (type == nullptr || depth > 32 || m_Variant.Layouts.Size() >= 4096 || type->getSize() == 0 || type->getSize() > 64u * 1024u * 1024u || type->getAlignment() <= 0)
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Unbounded or unresolved compiler data layout."));
		}

		shader::ShaderDataLayout layout(m_Resource);
		layout.ByteSize = type->getSize();
		layout.Alignment = static_cast<uint32>(type->getAlignment());
		layout.ElementStride = type->getStride();
		if (type->getKind() == slang::TypeReflection::Kind::Struct)
		{
			if (type->getFieldCount() > 65536)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Shader reflection field count is excessive."));
			}

			for (unsigned i = 0; i < type->getFieldCount(); ++i)
			{
				slang::VariableLayoutReflection* variable = type->getFieldByIndex(i);
				if (variable == nullptr || variable->getName() == nullptr)
				{
					return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Unnamed compiler data field."));
				}

				shader::ShaderDataField field(m_Resource);
				field.ByteOffset = variable->getOffset();
				if (!field.Name.Assign(StringView(variable->getName())))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				Result<void> result = reflectFieldRecursive(variable->getTypeLayout(), field, depth);
				if (!result)
				{
					return R::Failure(result.GetError());
				}

				if (!layout.Fields.Add(Move(field)))
				{
					return R::Failure(MakeBuildAllocationError());
				}
			}
		}
		else
		{
			shader::ShaderDataField field(m_Resource);
			if (!field.Name.Assign("$element"))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			Result<void> result = reflectFieldRecursive(type, field, depth);
			if (!result)
			{
				return R::Failure(result.GetError());
			}

			if (!layout.Fields.Add(Move(field)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
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

	Result<bool> SlangReflector::NativeParameter(slang::VariableLayoutReflection* variable, shader::EShaderStage stage, [[maybe_unused]] uint32 abi)
	{
		using R = Result<bool>;

		if (variable == nullptr || variable->getName() == nullptr || variable->getTypeLayout() == nullptr)
		{
			return R::Failure(reflectionFailure().GetError());
		}

		for (unsigned index = 0; index < variable->getCategoryCount(); ++index)
		{
			const slang::ParameterCategory category = variable->getCategoryByIndex(index);
			if (category == slang::ParameterCategory::ShaderRecord)
			{
				if (m_Variant.Target.Target != shader::EShaderTarget::SpirV || stage < shader::EShaderStage::RayGeneration || m_Variant.LocalRecord != shader::ShaderLocalRecord{})
				{
					return R::Failure(
						MakeBuildError(
						EErrorCode::Unsupported,
						"Local shader records require the exact Vulkan record profile; DXIL "
						"local root associations are unavailable."));
				}

				slang::TypeLayoutReflection* type = variable->getTypeLayout();
				if (type->getKind() != slang::TypeReflection::Kind::ConstantBuffer)
				{
					return R::Failure(reflectionFailure().GetError());
				}

				Result<uint32> reflected = reflectLayoutRecursive(type->getElementTypeLayout());
				if (!reflected)
				{
					return R::Failure(reflected.GetError());
				}

				const ShaderDataLayout& layout = m_Variant.Layouts[reflected.Value()];
				if (layout.ByteSize > 128)
				{
					return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Vulkan local record exceeds the verified 128-byte compiler profile."));
				}

				m_Variant.LocalRecord = {shader::EShaderLocalRecordLowering::VulkanShaderRecord, reflected.Value(), static_cast<uint32>(layout.ByteSize), layout.Alignment};
				{
					m_Variant.LocalRecord.Proof = shader::EShaderLocalRecordProof::FinalSpirVStorage;
				}

				return R::Success(true);
			}

			if (category != slang::ParameterCategory::PushConstantBuffer && category != slang::ParameterCategory::SpecializationConstant)
			{
				continue;
			}

			slang::TypeLayoutReflection* type = variable->getTypeLayout();
			if (category == slang::ParameterCategory::PushConstantBuffer)
			{
				const bool bVertexRaster = (stage == shader::EShaderStage::Vertex || stage == shader::EShaderStage::Pixel);
				const bool bMeshRaster = (stage == shader::EShaderStage::Mesh || stage == shader::EShaderStage::Pixel);

				if (stage != shader::EShaderStage::Compute && !bVertexRaster && !bMeshRaster)
				{
					return R::Failure(reflectionFailure().GetError());
				}

				if (type->getKind() != slang::TypeReflection::Kind::ConstantBuffer ||
					!m_Variant.PushConstants.IsEmpty() ||
					variable->getOffset(SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER) != 0)
				{
					return R::Failure(reflectionFailure().GetError());
				}

				Result<uint32> layout = reflectLayoutRecursive(type->getElementTypeLayout());
				if (!layout)
				{
					return R::Failure(layout.GetError());
				}

				shader::ShaderPushConstantRange range;
				const uint64 byteSize = m_Variant.Layouts[layout.Value()].ByteSize;
				if (byteSize > 128)
				{
					return R::Failure(reflectionFailure().GetError());
				}

				range.ByteSize = static_cast<uint32>(byteSize);
				range.DataLayout = layout.Value();
				range.StageMask = uint64{1} << static_cast<uint32>(stage);
				if (!m_Variant.PushConstants.Add(range))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				return R::Success(true);
			}

			if ((stage != shader::EShaderStage::Compute && stage != shader::EShaderStage::Vertex && stage != shader::EShaderStage::Pixel))
			{
				return R::Failure(reflectionFailure().GetError());
			}

			shader::ShaderSpecializationParameter parameter(m_Resource);
			const size_t id = variable->getOffset(SLANG_PARAMETER_CATEGORY_SPECIALIZATION_CONSTANT);
			if (id > 0xffffffffu || type->getKind() != slang::TypeReflection::Kind::Scalar || m_Variant.SpecializationParameters.Size() >= 64)
			{
				return R::Failure(reflectionFailure().GetError());
			}

			parameter.ID = static_cast<uint32>(id);

			switch (type->getScalarType())
			{
			case slang::TypeReflection::UInt32:
				parameter.Scalar = shader::EShaderScalar::UnsignedInteger;
				break;
			case slang::TypeReflection::Int32:
				parameter.Scalar = shader::EShaderScalar::SignedInteger;
				break;
			case slang::TypeReflection::Float32:
				parameter.Scalar = shader::EShaderScalar::Float;
				break;
			default:
				return R::Failure(reflectionFailure().GetError());
			}

			Slang::ComPtr<slang::IBlob> defaults;
			if (SLANG_FAILED(variable->getVariable()->getDefaultValueBlob(defaults.writeRef())) || !defaults || defaults->getBufferSize() != 4)
			{
				return R::Failure(reflectionFailure().GetError());
			}

			ByteReader reader({static_cast<const Byte*>(defaults->getBufferPointer()), 4});
			parameter.DefaultBits = reader.ReadU32LE();
			parameter.bHasDefault = true;
			if (!parameter.Name.Assign(StringView(variable->getName())) || !m_Variant.SpecializationParameters.Add(Move(parameter)))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			return R::Success(true);
		}

		return R::Success(false);
	}

	Result<uint32> SlangReflector::reflectPayloadTypeRecursive(Array<shader::ShaderPayloadType>& types, slang::TypeReflection* type, uint32 depth)
	{
		using R = Result<uint32>;

		using Kind = slang::TypeReflection::Kind;
		using Scalar = slang::TypeReflection::ScalarType;
		if (m_Cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		if (type == nullptr || depth > 16 || types.Size() >= 128)
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXIL mesh payload type exceeds its structural bounds."));
		}

		shader::ShaderPayloadType record(m_Resource);
		if (type->getKind() == Kind::Scalar)
		{
			record.Kind = shader::EShaderPayloadTypeKind::Scalar;
			record.ScalarWidth = 32;

			switch (type->getScalarType())
			{
			case Scalar::Float32:
				record.Scalar = shader::EShaderScalar::Float;
				break;
			case Scalar::Int32:
				record.Scalar = shader::EShaderScalar::SignedInteger;
				break;
			case Scalar::UInt32:
				record.Scalar = shader::EShaderScalar::UnsignedInteger;
				break;
			default:
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXIL mesh payload requires scalar32 numeric types."));
			}
		}
		else if (type->getKind() == Kind::Vector || type->getKind() == Kind::Array)
		{
			const size_t count = type->getElementCount();
			const bool bVector = type->getKind() == Kind::Vector;
			if (count == 0 || count > 4096 || (bVector && (count < 2 || count > 4)))
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXIL mesh payload array or vector is unsupported."));
			}

			Result<uint32> element = reflectPayloadTypeRecursive(types, type->getElementType(), depth + 1);
			if (!element)
			{
				return element;
			}

			record.Kind = bVector ? shader::EShaderPayloadTypeKind::Vector : shader::EShaderPayloadTypeKind::Array;
			record.Count = static_cast<uint32>(count);
			record.ElementType = element.Value();
		}
		else if (type->getKind() == Kind::Struct)
		{
			const uint32 count = type->getFieldCount();
			if (count == 0 || count > 32)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXIL mesh payload structure is unsupported."));
			}

			record.Kind = shader::EShaderPayloadTypeKind::Structure;
			if (!record.MemberTypes.Reserve(count))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			for (unsigned index = 0; index < count; ++index)
			{
				slang::VariableReflection* field = type->getFieldByIndex(index);
				Result<uint32> member = reflectPayloadTypeRecursive(types, field == nullptr ? nullptr : field->getType(), depth + 1);
				if (!member)
				{
					return member;
				}

				record.MemberTypes.AddRequired(member.Value());
			}
		}
		else
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXIL mesh payload type is unrepresented."));
		}

		if (types.Size() >= 128)
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXIL mesh payload has excessive type nodes."));
		}

		const uint32 index = static_cast<uint32>(types.Size());
		if (!types.Add(Move(record)))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		return R::Success(index);
	}

	Result<void> SlangReflector::RayParameters(slang::EntryPointReflection* entry)
	{
		using R = Result<void>;

		if (entry == nullptr || entry->getParameterCount() > 64)
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Ray entry parameter count is unsupported."));
		}

		for (unsigned index = 0; index < entry->getParameterCount(); ++index)
		{
			slang::VariableLayoutReflection* parameter = entry->getParameterByIndex(index);
			if (parameter == nullptr || parameter->getTypeLayout() == nullptr || parameter->getCategoryCount() != 1)
			{
				return reflectionFailure();
			}

			const slang::ParameterCategory category = parameter->getCategoryByIndex(0);
			shader::ShaderRayDataSignature data(m_Resource);
			if (category == slang::ParameterCategory::RayPayload)
			{
				data.Kind = shader::EShaderRayDataKind::Payload;
			}
			else if (category == slang::ParameterCategory::HitAttributes)
			{
				data.Kind = shader::EShaderRayDataKind::HitAttributes;
			}
			else if (category == slang::ParameterCategory::CallablePayload)
			{
				data.Kind = shader::EShaderRayDataKind::Callable;
			}
			else
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Ray entry parameter category is unrepresented."));
			}

			Result<uint32> root = reflectPayloadTypeRecursive(data.Types, parameter->getTypeLayout()->getType(), 1);
			if (!root)
			{
				return R::Failure(Move(root.GetError()));
			}

			data.RootType = root.Value();
			if (!m_Variant.RayInterface.Data.Add(Move(data)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		return R::Success();
	}

	Result<void> SlangReflector::MeshPayload(slang::EntryPointReflection* entry)
	{
		using R = Result<void>;

		if (entry == nullptr || entry->getParameterCount() > 4096)
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXIL mesh payload entry is unrepresented."));
		}

		bool bFound = false;

		for (unsigned index = 0; index < entry->getParameterCount(); ++index)
		{
			slang::VariableLayoutReflection* parameter = entry->getParameterByIndex(index);
			if (parameter == nullptr || parameter->getTypeLayout() == nullptr || parameter->getCategoryCount() > 32)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXIL mesh parameter is unrepresented."));
			}

			bool bInput = false;

			for (unsigned category = 0; category < parameter->getCategoryCount(); ++category)
			{
				bInput |= parameter->getCategoryByIndex(category) == slang::ParameterCategory::VaryingInput;
			}

			if (!bInput)
			{
				continue;
			}

			slang::TypeReflection* type = parameter->getTypeLayout()->getType();
			if (bFound || type == nullptr || type->getKind() != slang::TypeReflection::Kind::Struct)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXIL mesh requires one selected structural payload parameter."));
			}

			Result<uint32> root = reflectPayloadTypeRecursive(m_Variant.TaskPayload.Types, type, 1);
			if (!root)
			{
				return R::Failure(Move(root.GetError()));
			}

			m_Variant.TaskPayload.Proof = shader::EShaderTaskPayloadProof::Structural;
			m_Variant.TaskPayload.RootType = root.Value();
			bFound = true;
		}

		return R::Success();
	}

	Result<void> SlangReflector::Binding(slang::VariableLayoutReflection* variable)
	{
		using Kind = slang::TypeReflection::Kind;
		if (variable == nullptr || variable->getName() == nullptr || m_Variant.Bindings.Size() >= 4096)
		{
			return reflectionFailure();
		}

		slang::TypeLayoutReflection* type = variable->getTypeLayout();
		if (type == nullptr)
		{
			return reflectionFailure();
		}

		shader::ShaderBinding binding(m_Resource);
		slang::TypeLayoutReflection* descriptorLayout = type;
		if (!binding.Name.Assign(StringView(variable->getName())))
		{
			return Result<void>::Failure(MakeBuildAllocationError());
		}

		if (type->getKind() == Kind::Array)
		{
			const size_t count = type->getElementCount();
			// The pinned SDK returns zero for an unsized type despite its header comment.
			// The descriptor binding range below must independently prove it is unbounded.
			if ((count == 0 || count == SLANG_UNBOUNDED_SIZE))
			{
				binding.CountMode = shader::EShaderDescriptorCountMode::Runtime;
				binding.ArrayCount = 0;
				m_Variant.Requirements.Features |= static_cast<uint64>(shader::EShaderFeature::RuntimeDescriptorArray);
			}
			else
			{
				if (count == 0 || count > 65536)
				{
					return reflectionFailure();
				}

				binding.ArrayCount = static_cast<uint32>(count);
			}

			type = type->getElementTypeLayout();
			if (type == nullptr || type->getKind() == Kind::Array)
			{
				return reflectionFailure();
			}
		}

		SlangParameterCategory category = SLANG_PARAMETER_CATEGORY_NONE;
		slang::TypeLayoutReflection* data = nullptr;
		if (type->getKind() == Kind::ConstantBuffer)
		{
			binding.Kind = shader::EShaderBindingKind::ConstantBuffer;
			binding.RegisterNamespace = shader::EShaderRegisterClass::ConstantBuffer;
			category = SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER;
			data = type->getElementTypeLayout();
		}
		else if (type->getKind() == Kind::SamplerState)
		{
			binding.Kind = shader::EShaderBindingKind::Sampler;
			binding.RegisterNamespace = shader::EShaderRegisterClass::Sampler;
			category = SLANG_PARAMETER_CATEGORY_SAMPLER_STATE;
		}
		else if (type->getKind() == Kind::Resource)
		{
			const SlangResourceShape shape = type->getResourceShape();
			const SlangResourceShapeIntegral base = shape & SLANG_RESOURCE_BASE_SHAPE_MASK;
			const SlangResourceAccess access = type->getResourceAccess();
			if ((shape & (SLANG_TEXTURE_COMBINED_FLAG | SLANG_TEXTURE_FEEDBACK_FLAG)) != 0 ||
				(access != SLANG_RESOURCE_ACCESS_READ && access != SLANG_RESOURCE_ACCESS_READ_WRITE && access != SLANG_RESOURCE_ACCESS_WRITE))
			{
				return reflectionFailure();
			}

			binding.Access = access == SLANG_RESOURCE_ACCESS_READ ? shader::EShaderResourceAccess::Read
				: access == SLANG_RESOURCE_ACCESS_WRITE			  ? shader::EShaderResourceAccess::Write
																: shader::EShaderResourceAccess::ReadWrite;
			binding.RegisterNamespace = binding.Access == shader::EShaderResourceAccess::Read ? shader::EShaderRegisterClass::ShaderResource
																							: shader::EShaderRegisterClass::UnorderedAccess;
			category = binding.Access == shader::EShaderResourceAccess::Read ? SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE : SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS;
			if (base == SLANG_STRUCTURED_BUFFER || base == SLANG_BYTE_ADDRESS_BUFFER)
			{
				binding.Kind = binding.Access == shader::EShaderResourceAccess::Read ? shader::EShaderBindingKind::ReadOnlyBuffer : shader::EShaderBindingKind::ReadWriteBuffer;
				if (base == SLANG_STRUCTURED_BUFFER)
				{
					data = type->getElementTypeLayout();
					if (data == nullptr)
					{
						data = m_Session.getTypeLayout(type->getResourceResultType(), m_Target, slang::LayoutRules::DefaultStructuredBuffer);
					}
				}
			}
			else if (base == SLANG_ACCELERATION_STRUCTURE)
			{
				binding.Kind = shader::EShaderBindingKind::AccelerationStructure;
				m_Variant.RequiredCapabilities |= static_cast<uint64>(shader::EShaderCapability::RayTracing);
			}
			else
			{
				binding.Kind = binding.Access == shader::EShaderResourceAccess::Read ? shader::EShaderBindingKind::SampledTexture : shader::EShaderBindingKind::StorageTexture;

				switch (base)
				{
				case SLANG_TEXTURE_1D:
					binding.TextureShape = shader::EShaderTextureShape::Texture1D;
					break;
				case SLANG_TEXTURE_2D:
					binding.TextureShape = shader::EShaderTextureShape::Texture2D;
					break;
				case SLANG_TEXTURE_3D:
					binding.TextureShape = shader::EShaderTextureShape::Texture3D;
					break;
				case SLANG_TEXTURE_CUBE:
					binding.TextureShape = shader::EShaderTextureShape::Cube;
					break;
				case SLANG_TEXTURE_BUFFER:
					binding.TextureShape = shader::EShaderTextureShape::Buffer;
					break;
				default:
					return reflectionFailure();
				}

				binding.bTextureArray = (shape & SLANG_TEXTURE_ARRAY_FLAG) != 0;
				binding.bMultisampled = (shape & SLANG_TEXTURE_MULTISAMPLE_FLAG) != 0;
			}
		}
		else
		{
			return reflectionFailure();
		}

		if (m_Variant.Target.Target == shader::EShaderTarget::SpirV)
		{
			category = SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT;
			binding.RegisterNamespace = shader::EShaderRegisterClass::Unified;
		}

		if (binding.CountMode == shader::EShaderDescriptorCountMode::Runtime &&
			(descriptorLayout->getBindingRangeCount() != 1 || static_cast<usize>(descriptorLayout->getBindingRangeBindingCount(0)) != SLANG_UNBOUNDED_SIZE ||
				descriptorLayout->getSize(category) != (m_Variant.Target.Target == shader::EShaderTarget::SpirV ? 1 : SLANG_UNBOUNDED_SIZE)))
		{
			return reflectionFailure();
		}

		const size_t slot = variable->getOffset(category);
		const size_t space = variable->getBindingSpace(category);
		if (slot > 0xffffffffu || space > 0xffffffffu)
		{
			return reflectionFailure();
		}

		binding.Binding = static_cast<uint32>(slot);
		binding.Space = static_cast<uint32>(space);
		if (data != nullptr)
		{
			Result<uint32> layout = reflectLayoutRecursive(data);
			if (!layout)
			{
				return Result<void>::Failure(layout.GetError());
			}

			binding.DataLayout = layout.Value();
		}

		shader::ShaderResourceUse use;
		use.BindingIndex = static_cast<uint32>(m_Variant.Bindings.Size());
		use.Access = binding.Access;
		if (!m_Variant.Bindings.Add(Move(binding)) || !m_Variant.ResourceUses.Add(use))
		{
			return Result<void>::Failure(MakeBuildAllocationError());
		}

		return Result<void>::Success();
	}
} // namespace rox::shader
