#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE ToNvCoopType(ECoopVecDataType type) noexcept
	{
		switch (type)
		{
		case ECoopVecDataType::UInt8:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8;
		case ECoopVecDataType::SInt8:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT8;
		case ECoopVecDataType::UInt8Packed:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT8_PACKED;
		case ECoopVecDataType::SInt8Packed:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT8_PACKED;
		case ECoopVecDataType::UInt16:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT16;
		case ECoopVecDataType::SInt16:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT16;
		case ECoopVecDataType::UInt32:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT32;
		case ECoopVecDataType::SInt32:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT32;
		case ECoopVecDataType::UInt64:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_UINT64;
		case ECoopVecDataType::SInt64:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_SINT64;
		case ECoopVecDataType::FloatE4M3:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E4M3;
		case ECoopVecDataType::FloatE5M2:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT_E5M2;
		case ECoopVecDataType::Float16:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT16;
		case ECoopVecDataType::Float32:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT32;
		case ECoopVecDataType::Float64:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_FLOAT64;
		default:
			return NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_INVALID;
		}
	}
#endif
#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] ECoopVecDataType FromNvCoopType(NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE type) noexcept
	{
		if (type == NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_INVALID)
		{
			return static_cast<ECoopVecDataType>(UINT8_MAX);
		}

		for (uint32 value = 0; value <= static_cast<uint32>(ECoopVecDataType::Float64); ++value)
		{
			const ECoopVecDataType candidate = static_cast<ECoopVecDataType>(value);

			if (ToNvCoopType(candidate) == type)
			{
				return candidate;
			}
		}

		return static_cast<ECoopVecDataType>(UINT8_MAX);
	}
#endif
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

	[[nodiscard]] uint64 CoopVecStride(ECoopVecDataType type, ECoopVecMatrixLayout layout, uint32 rows, uint32 columns) noexcept
	{
		if (layout == ECoopVecMatrixLayout::RowMajor)
		{
			return (uint64{ columns } + 1) * CoopVecElementBytes(type);
		}

		if (layout == ECoopVecMatrixLayout::ColumnMajor)
		{
			return (uint64{ rows } + 1) * CoopVecElementBytes(type);
		}

		return 0;
	}
#if defined(DIRECT3D_LINEAR_ALGEBRA)
	[[nodiscard]] D3D12_LINEAR_ALGEBRA_DATATYPE ToLinAlgType(ECoopVecDataType type) noexcept
	{
		switch (type)
		{
		case ECoopVecDataType::UInt8:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_UINT8;
		case ECoopVecDataType::SInt8:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_SINT8;
		case ECoopVecDataType::UInt16:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_UINT16;
		case ECoopVecDataType::SInt16:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_SINT16;
		case ECoopVecDataType::UInt32:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_UINT32;
		case ECoopVecDataType::SInt32:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_SINT32;
		case ECoopVecDataType::FloatE4M3:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT8_E4M3FN;
		case ECoopVecDataType::FloatE5M2:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT8_E5M2;
		case ECoopVecDataType::Float16:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16;
		case ECoopVecDataType::Float32:
			return D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32;
		default:
			return static_cast<D3D12_LINEAR_ALGEBRA_DATATYPE>(0);
		}
	}
#endif
#if defined(DIRECT3D_LINEAR_ALGEBRA)
	[[nodiscard]] Result<void> InitLinAlg(State& state)
	{
		D3D12_FEATURE_DATA_LINEAR_ALGEBRA_SUPPORT support{};
		if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_LINEAR_ALGEBRA_SUPPORT, &support, sizeof(support))) ||
			support.LinearAlgebraTier == D3D12_LINEAR_ALGEBRA_TIER_NOT_SUPPORTED ||
			FAILED(state.Device.As(&state.LinAlgDevice)))
		{
			return Result<void>::Success();
		}
		constexpr ECoopVecDataType TYPES[]{ ECoopVecDataType::SInt8,	   ECoopVecDataType::UInt8,	  ECoopVecDataType::SInt16,
										   ECoopVecDataType::UInt16,   ECoopVecDataType::SInt32,  ECoopVecDataType::UInt32,
										   ECoopVecDataType::Float16,  ECoopVecDataType::Float32, ECoopVecDataType::FloatE4M3,
										   ECoopVecDataType::FloatE5M2 };
		Array<CoopVecFormatCombo> formats(state.Resource);

		for (const ECoopVecDataType input : TYPES)
		{
			for (const ECoopVecDataType matrix : TYPES)
			{
				for (const ECoopVecDataType bias : TYPES)
				{
					for (const ECoopVecDataType output : TYPES)
					{
						D3D12_FEATURE_DATA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT query{};
						query.OperationType = D3D12_LINEAR_ALGEBRA_OPERATION_TYPE_THREAD_VECTOR_MATRIX_MULTIPLY;
						query.ThreadVectorMatrixMultiply.VectorInputType = ToLinAlgType(input);
						query.ThreadVectorMatrixMultiply.MatrixInputType = ToLinAlgType(matrix);
						query.ThreadVectorMatrixMultiply.BiasInputType = ToLinAlgType(bias);
						query.ThreadVectorMatrixMultiply.VectorResultType = ToLinAlgType(output);

						if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_LINEAR_ALGEBRA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT, &query, sizeof(query))) ||
							(query.ThreadVectorMatrixMultiply.SupportFlags & D3D12_LINEAR_ALGEBRA_MULTIPLICATION_SUPPORT_FLAG_SUPPORTED) == 0)
						{
							continue;
						}
						// FP8 is packed into a native uint vector before interpretation in HLSL.
						const ECoopVecDataType inputStorage = input == ECoopVecDataType::FloatE4M3 || input == ECoopVecDataType::FloatE5M2 ? ECoopVecDataType::UInt32 : input;

						if (!formats.Add({ inputStorage, input, matrix, bias, output,
										  (query.ThreadVectorMatrixMultiply.SupportFlags &
										   D3D12_LINEAR_ALGEBRA_MULTIPLICATION_SUPPORT_FLAG_TRANSPOSE) != 0 }))
						{
							return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate linear algebra formats."));
						}
					}
				}
			}
		}

		if (formats.IsEmpty())
		{
			state.LinAlgDevice.Reset();

			return Result<void>::Success();
		}
		state.CoopVecFormats = Move(formats);
		state.bCooperativeVector = true;
		state.bNvCooperativeVector = false;
		const auto training = [&](D3D12_LINEAR_ALGEBRA_DATATYPE type) noexcept
			{
				D3D12_FEATURE_DATA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT outer{};
				outer.OperationType = D3D12_LINEAR_ALGEBRA_OPERATION_TYPE_THREAD_OUTER_PRODUCT;
				outer.ThreadOuterProductSupport.InputComponentType = type;
				outer.ThreadOuterProductSupport.ResultComponentType = type;
				D3D12_FEATURE_DATA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT accumulate{};
				accumulate.OperationType = D3D12_LINEAR_ALGEBRA_OPERATION_TYPE_ATOMIC_ACCUMULATE_STORE;
				accumulate.AccumulateStore.ComponentType = type;

				return SUCCEEDED(state.Device->CheckFeatureSupport(D3D12_FEATURE_LINEAR_ALGEBRA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT, &outer, sizeof(outer))) &&
					outer.ThreadOuterProductSupport.Supported &&
					SUCCEEDED(state.Device->CheckFeatureSupport(D3D12_FEATURE_LINEAR_ALGEBRA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT, &accumulate, sizeof(accumulate))) &&
					accumulate.AccumulateStore.RWByteAddressBufferSupported;
			};
		state.bCoopVecTrainingFloat16 = training(D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16);
		state.bCoopVecTrainingFloat32 = training(D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32);
		state.bCooperativeVectorTraining = state.bCoopVecTrainingFloat16 || state.bCoopVecTrainingFloat32;

		return Result<void>::Success();
	}
#endif
#if defined(DIRECT3D_LINEAR_ALGEBRA)
	[[nodiscard]] Result<uint64> GetLinAlgMatrixSize(const State& state, ECoopVecDataType type, ECoopVecMatrixLayout layout, uint32 rows, uint32 columns)
	{
		D3D12_LINEAR_ALGEBRA_MATRIX_CONVERSION_DEST_INFO info{};
		info.DestDataType = ToLinAlgType(type);
		info.DestLayout = static_cast<D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT>(layout);
		const uint64 stride = CoopVecStride(type, layout, rows, columns);

		if (stride > UINT32_MAX - 15 || static_cast<UINT>(info.DestDataType) == 0)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.D3D12LinAlg", "The matrix format or stride is unsupported.");
		}
		info.DestStride = static_cast<UINT>((stride + 15) & ~uint64{ 15 });
		info.NumRows = rows;
		info.NumColumns = columns;

		if (uint64{ rows } *columns > UINT32_MAX / CoopVecElementBytes(type))
		{
			return Result<uint64>::Failure(EErrorCode::InvalidArgument, "Rhi.D3D12LinAlg", "The matrix exceeds the native size limit.");
		}
		state.LinAlgDevice->GetLinearAlgebraMatrixConversionDestinationInfo(&info);

		if (info.DestSize == 0)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.D3D12LinAlg", "The matrix format is unsupported.");
		}
		// Native queries can omit padding after the last row. Conversion destinations require it.
		const uint64 alignedSize = (uint64{ info.DestSize } + 15) & ~uint64{ 15 };
		if (alignedSize > UINT32_MAX)
		{
			return Result<uint64>::Failure(EErrorCode::InvalidArgument, "Rhi.D3D12LinAlg", "The aligned matrix exceeds the native size limit.");
		}

		return Result<uint64>::Success(alignedSize);
	}
#endif
#if defined(DIRECT3D_LINEAR_ALGEBRA)
	[[nodiscard]] Result<void> RecordLinAlgMatrices(State& state, Pending& pending, Span<const NativeCoopVecMatrixConversion> conversions, bool automatic)
	{
		ComPtr<ID3D12GraphicsCommandListPreview> commands;

		if (FAILED(pending.List.As(&commands)))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12LinAlg", "The command list does not support linear algebra.");
		}

		Array<D3D12_LINEAR_ALGEBRA_MATRIX_CONVERSION_INFO> infos(state.Resource);

		if (!infos.ResizeUninitialized(conversions.Size()) || !ReserveTrackedStates(pending, pending.ResourceStates.Size() + conversions.Size() * 2))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate linear algebra conversions."));
		}

		for (usize index = 0; index < conversions.Size(); ++index)
		{
			const NativeCoopVecMatrixConversion& conversion = conversions[index];
			const CoopVecMatrixLocation& src = conversion.Descriptor.Source;
			const CoopVecMatrixLocation& dst = conversion.Descriptor.Destination;
			const uint64 sourceStride =
				src.StrideBytes != 0 ? src.StrideBytes
				: (CoopVecStride(src.Type, src.Layout, conversion.Descriptor.Rows, conversion.Descriptor.Columns) + 15) &
				~uint64{ 15 };
			const uint64 destinationStride =
				dst.StrideBytes != 0 ? dst.StrideBytes
				: (CoopVecStride(dst.Type, dst.Layout, conversion.Descriptor.Rows, conversion.Descriptor.Columns) + 15) &
				~uint64{ 15 };
			ASSERT(src.Type == dst.Type &&
				dst.OffsetBytes % 128 == 0 &&
				src.ByteCount <= UINT32_MAX &&
				dst.ByteCount <= UINT32_MAX &&
				sourceStride <= UINT32_MAX &&
				destinationStride <= UINT32_MAX &&
				sourceStride % 16 == 0 &&
				destinationStride % 16 == 0 &&
				dst.ByteCount % 16 == 0 &&
				static_cast >= UINT>(ToLinAlgType(src.Type)) == 0 &&
				static_cast >= UINT>(ToLinAlgType(dst.Type)) == 0,
				"Linear algebra matrix sizes and strides must fit native limits and alignment.");

			D3D12_LINEAR_ALGEBRA_MATRIX_CONVERSION_INFO& info = infos[index];
			info = {};
			info.DestInfo.DestDataType = ToLinAlgType(dst.Type);
			info.DestInfo.DestLayout = static_cast<D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT>(dst.Layout);
			info.DestInfo.DestStride = static_cast<UINT>(destinationStride);
			info.DestInfo.NumRows = conversion.Descriptor.Rows;
			info.DestInfo.NumColumns = conversion.Descriptor.Columns;
			state.LinAlgDevice->GetLinearAlgebraMatrixConversionDestinationInfo(&info.DestInfo);

			ASSERT(info.DestInfo.DestSize != 0 && info.DestInfo.DestSize <= dst.ByteCount, "The destination matrix range is too small.");
			info.DestInfo.DestSize = (info.DestInfo.DestSize + 15u) & ~UINT{ 15 };
			D3D12_LINEAR_ALGEBRA_MATRIX_CONVERSION_DEST_INFO sourceInfo{};
			sourceInfo.DestDataType = ToLinAlgType(src.Type);
			sourceInfo.DestLayout = static_cast<D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT>(src.Layout);
			sourceInfo.DestStride = static_cast<UINT>(sourceStride);
			sourceInfo.NumRows = conversion.Descriptor.Rows;
			sourceInfo.NumColumns = conversion.Descriptor.Columns;
			state.LinAlgDevice->GetLinearAlgebraMatrixConversionDestinationInfo(&sourceInfo);

			ASSERT(sourceInfo.DestSize != 0 && sourceInfo.DestSize <= src.ByteCount, "The source matrix range is too small.");
			info.SrcInfo.SrcSize = static_cast<UINT>(src.ByteCount);
			info.SrcInfo.SrcDataType = ToLinAlgType(src.Type);
			info.SrcInfo.SrcLayout = static_cast<D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT>(src.Layout);
			info.SrcInfo.SrcStride = static_cast<UINT>(sourceStride);
			info.DataDesc.SrcVA = static_cast<Buffer*>(conversion.Source)->Resource->GetGPUVirtualAddress() + src.OffsetBytes;
			info.DataDesc.DestVA = static_cast<Buffer*>(conversion.Destination)->Resource->GetGPUVirtualAddress() + dst.OffsetBytes;
		}

		for (const NativeCoopVecMatrixConversion& conversion : conversions)
		{
			Transition(pending, *static_cast<Buffer*>(conversion.Source)->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			Transition(pending, *static_cast<Buffer*>(conversion.Destination)->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12LinAlg", "Could not prepare conversion states."));
		}
		commands->ConvertLinearAlgebraMatrix(infos.Data(), static_cast<UINT>(infos.Size()));

		if (automatic)
		{
			for (const NativeCoopVecMatrixConversion& conversion : conversions)
			{
				EmitUavBarrier(pending, *static_cast<Buffer*>(conversion.Destination)->Resource.Get());
			}
		}

		return Result<void>::Success();
	}
#endif
	[[nodiscard]] Result<void> InitCoopVec(State& state)
	{
#if ROXELL_RHI_WITH_NVAPI
		NvU32 count = 0;

		if (NvAPI_D3D12_GetPhysicalDeviceCooperativeVectorProperties(state.Device.Get(), &count, nullptr) == NVAPI_OK && count != 0)
		{
			Array<NVAPI_COOPERATIVE_VECTOR_PROPERTIES> properties(state.Resource);

			if (!properties.ResizeUninitialized(count) || !state.CoopVecFormats.Reserve(count))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate cooperative vector formats."));
			}

			for (NVAPI_COOPERATIVE_VECTOR_PROPERTIES& property : properties)
			{
				property = {};
			}

			if (NvAPI_D3D12_GetPhysicalDeviceCooperativeVectorProperties(state.Device.Get(), &count, properties.Data()) == NVAPI_OK)
			{
				for (NvU32 index = 0; index < count; ++index)
				{
					const NVAPI_COOPERATIVE_VECTOR_PROPERTIES& property = properties[index];

					if (property.version != NVAPI_COOPERATIVE_VECTOR_PROPERTIES_VER)
					{
						continue;
					}

					const CoopVecFormatCombo combo{ FromNvCoopType(property.inputType),
												   FromNvCoopType(property.inputInterpretation),
												   FromNvCoopType(property.matrixInterpretation),
												   FromNvCoopType(property.biasInterpretation),
												   FromNvCoopType(property.resultType),
												   property.transpose };
					if (combo.InputType > ECoopVecDataType::Float64 ||
						combo.InputInterpretation > ECoopVecDataType::Float64 ||
						combo.MatrixInterpretation > ECoopVecDataType::Float64 ||
						combo.BiasInterpretation > ECoopVecDataType::Float64 ||
						combo.OutputType > ECoopVecDataType::Float64)
					{
						continue;
					}
					state.CoopVecFormats.AddRequired(combo);
				}
				state.bNvCooperativeVector = !state.CoopVecFormats.IsEmpty();
				state.bCooperativeVector = state.bNvCooperativeVector;
			}
		}
#else
		(void)state;
#endif
#if defined(DIRECT3D_LINEAR_ALGEBRA)
		return InitLinAlg(state);
#else
		return Result<void>::Success();
#endif
	}

	[[nodiscard]] Result<CoopVecFeatures> QueryCoopVecFeatures(const State* opaque, Span<CoopVecFormatCombo> formats)
	{
		const State& state = *opaque;

		if (!state.bCooperativeVector)
		{
			return Result<CoopVecFeatures>::Failure(EErrorCode::Unsupported, "Rhi.D3D12CoopVec", "Cooperative vectors are unavailable.");
		}
		CoopVecFeatures result{};
		result.FormatCount = static_cast<uint32>(state.CoopVecFormats.Size());
		result.bTrainingFloat16 = state.bCoopVecTrainingFloat16;
		result.bTrainingFloat32 = state.bCoopVecTrainingFloat32;
		result.MatrixStrideAlignmentBytes = state.bNvCooperativeVector ? 1 : 16;
		result.MatrixSizeAlignmentBytes = state.bNvCooperativeVector ? 1 : 16;
		result.MatrixOffsetAlignmentBytes = state.bNvCooperativeVector ? 64 : 128;
		result.ShaderDialect = state.bNvCooperativeVector ? ECoopVecShaderDialect::None : ECoopVecShaderDialect::DirectXLinAlg;

		if (!formats.IsEmpty())
		{
			if (formats.Size() < state.CoopVecFormats.Size())
			{
				return Result<CoopVecFeatures>::Failure(EErrorCode::InvalidArgument, "Rhi.D3D12CoopVec", "The format output is too small.");
			}

			for (usize index = 0; index < state.CoopVecFormats.Size(); ++index)
			{
				formats[index] = state.CoopVecFormats[index];
			}
		}

		return Result<CoopVecFeatures>::Success(result);
	}

	[[nodiscard]] Result<uint64> GetCoopVecMatrixSize(const State* opaque, ECoopVecDataType type, ECoopVecMatrixLayout layout, uint32 rows, uint32 columns)
	{
		const State& state = *opaque;
#if defined(DIRECT3D_LINEAR_ALGEBRA)
		if (state.LinAlgDevice)
		{
			return GetLinAlgMatrixSize(state, type, layout, rows, columns);
		}
#endif
#if ROXELL_RHI_WITH_NVAPI
		if (state.bNvCooperativeVector && ToNvCoopType(type) != NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_INVALID)
		{
			size_t bytes = 0;
			NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC info{};
			info.version = NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC_VER;
			info.pDstSize = &bytes;
			info.srcComponentType = ToNvCoopType(type);
			info.dstComponentType = info.srcComponentType;
			info.numRows = rows;
			info.numColumns = columns;
			info.srcLayout = NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR;
			info.srcStride = static_cast<size_t>(CoopVecStride(type, ECoopVecMatrixLayout::RowMajor, rows, columns));
			info.dstLayout = static_cast<NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT>(layout);
			info.dstStride = static_cast<size_t>(CoopVecStride(type, layout, rows, columns));
			const uint64 rowBytes = CoopVecElementBytes(type) * columns;

			if (rows > 1 && info.srcStride > (SIZE_MAX - rowBytes) / (rows - 1))
			{
				return Result<uint64>::Failure(EErrorCode::InvalidArgument, "Rhi.D3D12CoopVec", "The matrix size overflows.");
			}
			info.srcSize = static_cast<size_t>((rows - 1) * info.srcStride + rowBytes);

			if (NvAPI_D3D12_ConvertCooperativeVectorMatrix(state.Device.Get(), nullptr, &info) == NVAPI_OK && bytes != 0)
			{
				if (layout == ECoopVecMatrixLayout::RowMajor || layout == ECoopVecMatrixLayout::ColumnMajor)
				{
					const uint64 major = layout == ECoopVecMatrixLayout::RowMajor ? rows : columns;
					const uint64 minor = layout == ECoopVecMatrixLayout::RowMajor ? columns : rows;
					const uint64 finalBytes = minor * CoopVecElementBytes(type);

					if (major > 1 && info.dstStride > (SIZE_MAX - finalBytes) / (major - 1))
					{
						return Result<uint64>::Failure(EErrorCode::InvalidArgument, "Rhi.D3D12CoopVec", "The matrix size overflows.");
					}

					const uint64 required = (major - 1) * info.dstStride + finalBytes;

					if (bytes < required)
					{
						bytes = static_cast<size_t>(required);
					}
				}

				return Result<uint64>::Success(bytes);
			}
		}
#else
		(void)state;
		(void)type;
		(void)layout;
		(void)rows;
		(void)columns;
#endif
		return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.D3D12CoopVec", "The matrix format is unsupported.");
	}

	[[nodiscard]] Result<void> RecordCoopVecMatrices(State* opaque, void* native, Span<const NativeCoopVecMatrixConversion> conversions, bool bAutomatic)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
#if defined(DIRECT3D_LINEAR_ALGEBRA)
		if (state.LinAlgDevice)
		{
			return RecordLinAlgMatrices(state, pending, conversions, automatic);
		}
#endif
#if ROXELL_RHI_WITH_NVAPI
		if (state.bNvCooperativeVector)
		{
			Array<NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC> infos(state.Resource);
			Array<size_t> sizes(state.Resource);

			if (!infos.ResizeUninitialized(conversions.Size()) ||
				!sizes.ResizeUninitialized(conversions.Size()) ||
				!ReserveTrackedStates(pending, pending.ResourceStates.Size() + conversions.Size() * 2))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate matrix conversion descriptors."));
			}

			for (usize index = 0; index < conversions.Size(); ++index)
			{
				const NativeCoopVecMatrixConversion& conversion = conversions[index];
				const CoopVecMatrixLocation& src = conversion.Descriptor.Source;
				const CoopVecMatrixLocation& dst = conversion.Descriptor.Destination;
				NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC& info = infos[index];
				info = {};
				info.version = NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC_VER;
				info.srcComponentType = ToNvCoopType(src.Type);
				info.dstComponentType = ToNvCoopType(dst.Type);

				if (info.srcComponentType == NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_INVALID || info.dstComponentType == NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE_INVALID)
				{
					return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12CoopVec", "The conversion format is unsupported.");
				}
				info.numRows = conversion.Descriptor.Rows;
				info.numColumns = conversion.Descriptor.Columns;
				info.srcSize = static_cast<size_t>(src.ByteCount);
				info.srcLayout = static_cast<NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT>(src.Layout);
				info.dstLayout = static_cast<NVAPI_COOPERATIVE_VECTOR_MATRIX_LAYOUT>(dst.Layout);
				info.srcStride = static_cast<size_t>(src.StrideBytes != 0 ? src.StrideBytes : CoopVecStride(src.Type, src.Layout, info.numRows, info.numColumns));
				info.dstStride = static_cast<size_t>(dst.StrideBytes != 0 ? dst.StrideBytes : CoopVecStride(dst.Type, dst.Layout, info.numRows, info.numColumns));
				sizes[index] = static_cast<size_t>(dst.ByteCount);
				info.pDstSize = &sizes[index];
				info.srcData.bIsDeviceAlloc = true;
				info.srcData.deviceAddress = static_cast<Buffer*>(conversion.Source)->Resource->GetGPUVirtualAddress() + src.OffsetBytes;
				info.dstData.bIsDeviceAlloc = true;
				info.dstData.deviceAddress = static_cast<Buffer*>(conversion.Destination)->Resource->GetGPUVirtualAddress() + dst.OffsetBytes;
			}

			for (const NativeCoopVecMatrixConversion& conversion : conversions)
			{
				Transition(pending, *static_cast<Buffer*>(conversion.Source)->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				Transition(pending, *static_cast<Buffer*>(conversion.Destination)->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			}

			if (pending.bInvalidResourceState)
			{
				return Result<void>::Failure(Failure("Rhi.D3D12CoopVec", "Could not prepare conversion states."));
			}

			if (NvAPI_D3D12_ConvertCooperativeVectorMatrixMultiple(state.Device.Get(), pending.List.Get(), infos.Data(),
				static_cast<NvU32>(infos.Size())) != NVAPI_OK)
			{
				return Result<void>::Failure(Failure("Rhi.D3D12CoopVec", "Could not record matrix conversions."));
			}

			if (automatic)
			{
				for (const NativeCoopVecMatrixConversion& conversion : conversions)
				{
					EmitUavBarrier(pending, *static_cast<Buffer*>(conversion.Destination)->Resource.Get());
				}
			}

			return Result<void>::Success();
		}
#else
		(void)state;
		(void)pending;
		(void)conversions;
		(void)bAutomatic;
#endif
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12CoopVec", "Cooperative vector conversion is unavailable.");
	}
}
#endif
