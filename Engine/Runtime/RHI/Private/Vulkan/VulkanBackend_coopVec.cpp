#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] VkComponentTypeKHR ConvertCoopVecType(ECoopVecDataType type) noexcept
	{
		switch (type)
		{
		case ECoopVecDataType::UInt8:
			return VK_COMPONENT_TYPE_UINT8_KHR;
		case ECoopVecDataType::SInt8:
			return VK_COMPONENT_TYPE_SINT8_KHR;
		case ECoopVecDataType::UInt8Packed:
			return VK_COMPONENT_TYPE_UINT8_PACKED_NV;
		case ECoopVecDataType::SInt8Packed:
			return VK_COMPONENT_TYPE_SINT8_PACKED_NV;
		case ECoopVecDataType::UInt16:
			return VK_COMPONENT_TYPE_UINT16_KHR;
		case ECoopVecDataType::SInt16:
			return VK_COMPONENT_TYPE_SINT16_KHR;
		case ECoopVecDataType::UInt32:
			return VK_COMPONENT_TYPE_UINT32_KHR;
		case ECoopVecDataType::SInt32:
			return VK_COMPONENT_TYPE_SINT32_KHR;
		case ECoopVecDataType::UInt64:
			return VK_COMPONENT_TYPE_UINT64_KHR;
		case ECoopVecDataType::SInt64:
			return VK_COMPONENT_TYPE_SINT64_KHR;
		case ECoopVecDataType::FloatE4M3:
			return VK_COMPONENT_TYPE_FLOAT_E4M3_NV;
		case ECoopVecDataType::FloatE5M2:
			return VK_COMPONENT_TYPE_FLOAT_E5M2_NV;
		case ECoopVecDataType::Float16:
			return VK_COMPONENT_TYPE_FLOAT16_KHR;
		case ECoopVecDataType::BFloat16:
			return VK_COMPONENT_TYPE_BFLOAT16_KHR;
		case ECoopVecDataType::Float32:
			return VK_COMPONENT_TYPE_FLOAT32_KHR;
		case ECoopVecDataType::Float64:
			return VK_COMPONENT_TYPE_FLOAT64_KHR;

		default:
		{
			break;
		}
		}

		return VK_COMPONENT_TYPE_MAX_ENUM_KHR;
	}

	[[nodiscard]] ECoopVecDataType FromCoopVecType(VkComponentTypeKHR type) noexcept
	{
		switch (type)
		{
		case VK_COMPONENT_TYPE_UINT8_KHR:
			return ECoopVecDataType::UInt8;
		case VK_COMPONENT_TYPE_SINT8_KHR:
			return ECoopVecDataType::SInt8;
		case VK_COMPONENT_TYPE_UINT8_PACKED_NV:
			return ECoopVecDataType::UInt8Packed;
		case VK_COMPONENT_TYPE_SINT8_PACKED_NV:
			return ECoopVecDataType::SInt8Packed;
		case VK_COMPONENT_TYPE_UINT16_KHR:
			return ECoopVecDataType::UInt16;
		case VK_COMPONENT_TYPE_SINT16_KHR:
			return ECoopVecDataType::SInt16;
		case VK_COMPONENT_TYPE_UINT32_KHR:
			return ECoopVecDataType::UInt32;
		case VK_COMPONENT_TYPE_SINT32_KHR:
			return ECoopVecDataType::SInt32;
		case VK_COMPONENT_TYPE_UINT64_KHR:
			return ECoopVecDataType::UInt64;
		case VK_COMPONENT_TYPE_SINT64_KHR:
			return ECoopVecDataType::SInt64;
		case VK_COMPONENT_TYPE_FLOAT_E4M3_NV:
			return ECoopVecDataType::FloatE4M3;
		case VK_COMPONENT_TYPE_FLOAT_E5M2_NV:
			return ECoopVecDataType::FloatE5M2;
		case VK_COMPONENT_TYPE_FLOAT16_KHR:
			return ECoopVecDataType::Float16;
		case VK_COMPONENT_TYPE_BFLOAT16_KHR:
			return ECoopVecDataType::BFloat16;
		case VK_COMPONENT_TYPE_FLOAT32_KHR:
			return ECoopVecDataType::Float32;
		case VK_COMPONENT_TYPE_FLOAT64_KHR:
			return ECoopVecDataType::Float64;
		default:
			return static_cast<ECoopVecDataType>(UINT8_MAX);
		}
	}

	[[nodiscard]] VkCooperativeVectorMatrixLayoutNV ConvertCoopVecLayout(ECoopVecMatrixLayout layout) noexcept
	{
		switch (layout)
		{
		case ECoopVecMatrixLayout::RowMajor:
			return VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV;
		case ECoopVecMatrixLayout::ColumnMajor:
			return VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_COLUMN_MAJOR_NV;
		case ECoopVecMatrixLayout::InferencingOptimal:
			return VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_INFERENCING_OPTIMAL_NV;
		case ECoopVecMatrixLayout::TrainingOptimal:
			return VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_TRAINING_OPTIMAL_NV;

		default:
		{
			break;
		}
		}

		return VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_MAX_ENUM_NV;
	}

	[[nodiscard]] uint64 CoopVecElementBytes(ECoopVecDataType type) noexcept
	{
		if (type == ECoopVecDataType::UInt64 || type == ECoopVecDataType::SInt64 || type == ECoopVecDataType::Float64)
		{
			return 8;
		}

		if (type == ECoopVecDataType::UInt32 || type == ECoopVecDataType::SInt32 || type == ECoopVecDataType::Float32)
		{
			return 4;
		}

		if (type == ECoopVecDataType::UInt16 || type == ECoopVecDataType::SInt16 || type == ECoopVecDataType::Float16 || type == ECoopVecDataType::BFloat16)
		{
			return 2;
		}

		return 1;
	}

	[[nodiscard]] Result<CoopVecFeatures> QueryCoopVecFeatures(const State* opaque, Span<CoopVecFormatCombo> formats)
	{
		const State& state = *opaque;

		if (!state.bCooperativeVector || state.F.GetCoopVecProperties == nullptr)
		{
			return Result<CoopVecFeatures>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCoopVec", "Cooperative vectors are unavailable.");
		}

		uint32 count = 0;

		if (state.F.GetCoopVecProperties(state.Physical, &count, nullptr) != VK_SUCCESS)
		{
			return Result<CoopVecFeatures>::Failure(Failure("Rhi.VulkanCoopVec", "Could not query cooperative vector property count."));
		}
		CoopVecFeatures result{};
		result.ShaderDialect = state.ApiVersion >= VK_API_VERSION_1_3 ? ECoopVecShaderDialect::SpirvNV : ECoopVecShaderDialect::None;
		result.FormatCount = count;
		result.bTrainingFloat16 = state.CoopVecProperties.cooperativeVectorTrainingFloat16Accumulation == VK_TRUE;
		result.bTrainingFloat32 = state.CoopVecProperties.cooperativeVectorTrainingFloat32Accumulation == VK_TRUE;

		if (formats.IsEmpty())
		{
			return Result<CoopVecFeatures>::Success(result);
		}

		if (formats.Size() < count)
		{
			return Result<CoopVecFeatures>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanCoopVec", "The format output is too small.");
		}

		Array<VkCooperativeVectorPropertiesNV> properties(state.Resource);

		if (!properties.ResizeUninitialized(count))
		{
			return Result<CoopVecFeatures>::Failure(Failure("Rhi.Allocation", "Could not allocate cooperative vector properties."));
		}

		for (VkCooperativeVectorPropertiesNV& property : properties)
		{
			property = { VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV };
		}

		const VkResult read = state.F.GetCoopVecProperties(state.Physical, &count, properties.Data());

		if (read != VK_SUCCESS)
		{
			return Result<CoopVecFeatures>::Failure(Failure("Rhi.VulkanCoopVec", "Could not read cooperative vector properties."));
		}

		for (uint32 index = 0; index < count; ++index)
		{
			VkCooperativeVectorPropertiesNV const& property = properties[index];
			formats[index] = { FromCoopVecType(property.inputType),
							  FromCoopVecType(property.inputInterpretation),
							  FromCoopVecType(property.matrixInterpretation),
							  FromCoopVecType(property.biasInterpretation),
							  FromCoopVecType(property.resultType),
							  property.transpose == VK_TRUE };
		}
		result.FormatCount = count;

		return Result<CoopVecFeatures>::Success(result);
	}

	[[nodiscard]] Result<uint64> GetCoopVecMatrixSize(const State* opaque, ECoopVecDataType type, ECoopVecMatrixLayout layout, uint32 rows, uint32 columns)
	{
		const State& state = *opaque;

		if (!state.bCooperativeVector || state.F.ConvertCoopVecMatrix == nullptr)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCoopVec", "Cooperative vectors are unavailable.");
		}

		const uint64 unit = CoopVecElementBytes(type);
		const uint64 rowBytes = unit * columns;
		const uint64 pitch = rowBytes + unit;

		if (rows > 1 && pitch > (SIZE_MAX - rowBytes) / (rows - 1))
		{
			return Result<uint64>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanCoopVec", "The matrix size overflows.");
		}
		size_t bytes = 0;
		VkConvertCooperativeVectorMatrixInfoNV info{ VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV };
		info.srcSize = static_cast<size_t>((rows - 1) * pitch + rowBytes);
		info.pDstSize = &bytes;
		info.srcComponentType = ConvertCoopVecType(type);
		info.dstComponentType = info.srcComponentType;
		info.numRows = rows;
		info.numColumns = columns;
		info.srcLayout = VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV;
		info.srcStride = static_cast<size_t>(pitch);
		info.dstLayout = ConvertCoopVecLayout(layout);
		info.dstStride = layout == ECoopVecMatrixLayout::RowMajor ? static_cast<size_t>(pitch)
			: layout == ECoopVecMatrixLayout::ColumnMajor ? static_cast<size_t>(unit * (rows + uint64{ 1 }))
			: 0;

		if (state.F.ConvertCoopVecMatrix(state.Device, &info) != VK_SUCCESS || bytes == 0)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCoopVec", "The matrix format is unsupported.");
		}

		if (layout == ECoopVecMatrixLayout::RowMajor && bytes < info.srcSize)
		{
			bytes = info.srcSize;
		}

		if (layout == ECoopVecMatrixLayout::ColumnMajor)
		{
			const uint64 columnBytes = unit * rows;
			const uint64 columnPitch = columnBytes + unit;

			if (columns > 1 && columnPitch > (SIZE_MAX - columnBytes) / (columns - 1))
			{
				return Result<uint64>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanCoopVec", "The matrix size overflows.");
			}

			const size_t required = static_cast<size_t>((columns - 1) * columnPitch + columnBytes);

			if (bytes < required)
			{
				bytes = required;
			}
		}

		return Result<uint64>::Success(bytes);
	}

	[[nodiscard]] Result<void> RecordCoopVecMatrices(State* opaque, void* native, Span<const NativeCoopVecMatrixConversion> conversions, bool bAutomatic)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);

		if (!state.bCooperativeVector || state.F.CmdConvertCoopVecMatrix == nullptr)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCoopVec", "Cooperative vector conversion is unavailable.");
		}

		Array<VkConvertCooperativeVectorMatrixInfoNV> infos(state.Resource);
		Array<size_t> outputSizes(state.Resource);

		if (!infos.ResizeUninitialized(conversions.Size()) || !outputSizes.ResizeUninitialized(conversions.Size()))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate cooperative vector conversion descriptors."));
		}

		for (usize index = 0; index < conversions.Size(); ++index)
		{
			const rhi::NativeCoopVecMatrixConversion& conversion = conversions[index];
			const CoopVecMatrixLocation& src = conversion.Descriptor.Source;
			const CoopVecMatrixLocation& dst = conversion.Descriptor.Destination;

			ASSERT(src.ByteCount <= SIZE_MAX && dst.ByteCount <= SIZE_MAX && src.StrideBytes <= SIZE_MAX && dst.StrideBytes <= SIZE_MAX, "The conversion size exceeds the native size limit.");
			VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			addressInfo.buffer = static_cast<Buffer*>(conversion.Source)->Handle;
			const VkDeviceAddress sourceBase = state.F.GetBufferDeviceAddress(state.Device, &addressInfo);
			addressInfo.buffer = static_cast<Buffer*>(conversion.Destination)->Handle;
			const VkDeviceAddress destinationBase = state.F.GetBufferDeviceAddress(state.Device, &addressInfo);

			if (sourceBase == 0 || destinationBase == 0 || src.OffsetBytes > UINT64_MAX - sourceBase || dst.OffsetBytes > UINT64_MAX - destinationBase)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanCoopVec", "A conversion buffer has no valid device address."));
			}

			const VkDeviceAddress sourceAddress = sourceBase + src.OffsetBytes;
			const VkDeviceAddress destinationAddress = destinationBase + dst.OffsetBytes;

			if (sourceAddress % 64 != 0 || destinationAddress % 64 != 0)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanCoopVec", "A conversion buffer has no aligned device address."));
			}
			outputSizes[index] = static_cast<size_t>(dst.ByteCount);
			VkConvertCooperativeVectorMatrixInfoNV& info = infos[index];
			info = { VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV };
			info.srcSize = static_cast<size_t>(src.ByteCount);
			info.srcData.deviceAddress = sourceAddress;
			info.pDstSize = &outputSizes[index];
			info.dstData.deviceAddress = destinationAddress;
			info.srcComponentType = ConvertCoopVecType(src.Type);
			info.dstComponentType = ConvertCoopVecType(dst.Type);
			info.numRows = conversion.Descriptor.Rows;
			info.numColumns = conversion.Descriptor.Columns;
			info.srcLayout = ConvertCoopVecLayout(src.Layout);
			info.dstLayout = ConvertCoopVecLayout(dst.Layout);
			info.srcStride =
				src.StrideBytes != 0
				? static_cast<size_t>(src.StrideBytes)
				: (src.Layout == ECoopVecMatrixLayout::RowMajor || src.Layout == ECoopVecMatrixLayout::ColumnMajor
					? static_cast<size_t>(CoopVecElementBytes(src.Type) * (src.Layout == ECoopVecMatrixLayout::RowMajor
						? uint64{ conversion.Descriptor.Columns } + 1
						: uint64{ conversion.Descriptor.Rows } + 1))
					: 0);
			info.dstStride =
				dst.StrideBytes != 0
				? static_cast<size_t>(dst.StrideBytes)
				: (dst.Layout == ECoopVecMatrixLayout::RowMajor || dst.Layout == ECoopVecMatrixLayout::ColumnMajor
					? static_cast<size_t>(CoopVecElementBytes(dst.Type) * (dst.Layout == ECoopVecMatrixLayout::RowMajor
						? uint64{ conversion.Descriptor.Columns } + 1
						: uint64{ conversion.Descriptor.Rows } + 1))
					: 0);
		}

		if (bAutomatic)
		{
			VkMemoryBarrier before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			before.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &before, 0,
				nullptr, 0, nullptr);
		}
		state.F.CmdConvertCoopVecMatrix(pending.Commands, static_cast<uint32>(infos.Size()), infos.Data());

		if (bAutomatic)
		{
			VkMemoryBarrier after{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			after.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			after.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, nullptr,
				0, nullptr);
		}

		return Result<void>::Success();
	}
}
