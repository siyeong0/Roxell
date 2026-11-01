#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<CoopVecFeatures> Device::QueryCoopVecFeatures(Span<CoopVecFormatCombo> formats) const
	{
		CallScope call(*this, true);

		if (!m_State->m_Backend.bCooperativeVector || backend::HAS_QUERY_COOP_VEC_FEATURES == false)
		{
			return Result<CoopVecFeatures>::Failure(EErrorCode::Unsupported, "Rhi.CoopVec", "Cooperative vectors are unavailable.");
		}

		return backend::QueryCoopVecFeatures(m_State->m_Backend.State, formats);
	}

	[[nodiscard]] Result<uint64> Device::GetCoopVecMatrixSize(ECoopVecDataType type, ECoopVecMatrixLayout layout, uint32 rows, uint32 columns) const
	{
		CallScope call(*this, true);

		ASSERT(type <= ECoopVecDataType::Float64 && layout <= ECoopVecMatrixLayout::TrainingOptimal && rows != 0 && columns != 0, "The cooperative vector matrix type, layout, and shape must be valid.");

		if (!m_State->m_Backend.bCooperativeVector || backend::HAS_GET_COOP_VEC_MATRIX_SIZE == false)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.CoopVec", "Cooperative vectors are unavailable.");
		}

		if (layout == ECoopVecMatrixLayout::TrainingOptimal && !m_State->m_Backend.bCooperativeVectorTraining)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.CoopVec", "Cooperative vector training is unavailable.");
		}

		return backend::GetCoopVecMatrixSize(m_State->m_Backend.State, type, layout, rows, columns);
	}

	[[nodiscard]] Result<void> Device::ConvertCoopVecMatrices(CommandListHandle list, Span<const CoopVecMatrixConversion> conversions)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(command->Queue != EQueue::Copy && !command->bRendering, "Matrix conversion requires a graphics or compute list outside rendering.");
		ASSERT(!conversions.IsEmpty() && conversions.Size() <= UINT32_MAX, "Matrix conversion requires a nonempty bounded batch.");

		if (!m_State->m_Backend.bCooperativeVector || backend::HAS_RECORD_COOP_VEC_MATRICES == false)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.CoopVec", "This device cannot convert cooperative vector matrices.");
		}

		Array<NativeCoopVecMatrixConversion> native(m_State->m_Resource);

		if (!native.Reserve(conversions.Size()) || !command->BufferUses.Reserve(command->BufferUses.Size() + conversions.Size() * 2))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain matrix conversion resources.");
		}

		for (const CoopVecMatrixConversion& conversion : conversions)
		{
			const CoopVecMatrixLocation& src = conversion.Source;
			const CoopVecMatrixLocation& dst = conversion.Destination;
			BufferSlot* const source = findBuffer(src.Buffer);
			BufferSlot* const destination = findBuffer(dst.Buffer);

			ASSERT(!(source == nullptr || destination == nullptr), "RHI operations require valid handles for this device.");

			ASSERT(src.Buffer != dst.Buffer, "Matrix conversion source and destination require different buffers.");

			ASSERT(!((dst.Type == ECoopVecDataType::FloatE4M3 || dst.Type == ECoopVecDataType::FloatE5M2) &&
					(dst.Layout == ECoopVecMatrixLayout::RowMajor || dst.Layout == ECoopVecMatrixLayout::ColumnMajor)),
				"Low precision float matrix output requires an optimal layout.");

			if (src.Type != dst.Type)
			{
				const auto lowerFloat = [](ECoopVecDataType type) noexcept
					{
						return type == ECoopVecDataType::Float16 || type == ECoopVecDataType::BFloat16 || type == ECoopVecDataType::FloatE4M3 || type == ECoopVecDataType::FloatE5M2;
					};
				ASSERT((src.Type == ECoopVecDataType::Float32 && lowerFloat(dst.Type)) ||
					(dst.Type == ECoopVecDataType::Float32 && lowerFloat(src.Type)) ||
					(src.Type == ECoopVecDataType::Float16 && (dst.Type == ECoopVecDataType::FloatE4M3 || dst.Type == ECoopVecDataType::FloatE5M2)) ||
					(dst.Type == ECoopVecDataType::Float16 && (src.Type == ECoopVecDataType::FloatE4M3 || src.Type == ECoopVecDataType::FloatE5M2)),
					"Matrix conversion requires compatible source and destination types.");
			}

			ASSERT(!source->bMapped && !destination->bMapped && source->Memory == EBufferMemory::Device && destination->Memory == EBufferMemory::Device, "Matrix conversion requires unmapped device buffers.");

			ASSERT(!(!HasUsage(source->Usage, EBufferUsage::DeviceAddress) ||
					!HasUsage(destination->Usage, EBufferUsage::DeviceAddress) ||
					!HasUsage(source->Usage, EBufferUsage::Storage) ||
					!HasUsage(destination->Usage, EBufferUsage::Storage) ||
					!Fits(source->ByteCount, src.OffsetBytes, src.ByteCount) ||
					!Fits(destination->ByteCount, dst.OffsetBytes, dst.ByteCount) ||
					src.OffsetBytes % 64 != 0 ||
					dst.OffsetBytes % 64 != 0 ||
					conversion.Rows == 0 ||
					conversion.Columns == 0 ||
					src.Type > ECoopVecDataType::Float64 ||
					dst.Type > ECoopVecDataType::Float64 ||
					src.Layout > ECoopVecMatrixLayout::TrainingOptimal ||
					dst.Layout > ECoopVecMatrixLayout::TrainingOptimal),
				"Matrix conversion requires compatible buffer usage, contained aligned ranges and valid matrix shapes.");
			if
				((src.Layout == ECoopVecMatrixLayout::TrainingOptimal || dst.Layout == ECoopVecMatrixLayout::TrainingOptimal) && !m_State->m_Backend.bCooperativeVectorTraining)
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.CoopVec", "The device does not support cooperative vector training layouts.");
			}

			const Result<uint64> sourceBytes = GetCoopVecMatrixSize(src.Type, src.Layout, conversion.Rows, conversion.Columns);
			const Result<uint64> destinationBytes = GetCoopVecMatrixSize(dst.Type, dst.Layout, conversion.Rows, conversion.Columns);

			if (!sourceBytes || !destinationBytes)
			{
				poisonCommandList(list);

				return Result<void>::Failure(!sourceBytes ? sourceBytes.GetError() : destinationBytes.GetError());
			}

			const auto standardSpan = [&](const CoopVecMatrixLocation& location) noexcept
				{
					if (location.Layout == ECoopVecMatrixLayout::InferencingOptimal || location.Layout == ECoopVecMatrixLayout::TrainingOptimal)
					{
						return location.StrideBytes == 0 ? uint64{ 1 } : uint64{ 0 };
					}

					const uint64 unit = location.Type == ECoopVecDataType::UInt64 || location.Type == ECoopVecDataType::SInt64 ||
						location.Type == ECoopVecDataType::Float64
						? 8
						: location.Type == ECoopVecDataType::UInt32 || location.Type == ECoopVecDataType::SInt32 ||
						location.Type == ECoopVecDataType::Float32
						? 4
						: location.Type == ECoopVecDataType::UInt16 || location.Type == ECoopVecDataType::SInt16 ||
						location.Type == ECoopVecDataType::Float16 || location.Type == ECoopVecDataType::BFloat16
						? 2
						: 1;
					const uint64 minor = location.Layout == ECoopVecMatrixLayout::RowMajor ? conversion.Columns : conversion.Rows;
					const uint64 major = location.Layout == ECoopVecMatrixLayout::RowMajor ? conversion.Rows : conversion.Columns;
					const uint64 rowBytes = unit * minor;
					const uint64 pitch = location.StrideBytes != 0 ? location.StrideBytes : rowBytes + unit;

					if (pitch <= rowBytes || pitch % unit != 0 || pitch > (UINT64_MAX - rowBytes) / (major - 1 == 0 ? 1 : major - 1))
					{
						return uint64{ 0 };
					}

					return (major - 1) * pitch + rowBytes;
				};
			const uint64 sourceSpan = standardSpan(src);
			const uint64 destinationSpan = standardSpan(dst);

			ASSERT(sourceSpan != 0 && destinationSpan != 0, "Matrix conversion strides must describe valid storage.");
			ASSERT(src.ByteCount >= sourceBytes.Value() && dst.ByteCount >= destinationBytes.Value() && src.ByteCount >= sourceSpan && dst.ByteCount >= destinationSpan,
				"Matrix conversion ranges must contain the complete matrix storage.");

			ASSERT(command->bAutomaticBarriers || (command->HasBufferState(src.Buffer, ETrackedResourceState::ShaderRead) && command->HasBufferState(dst.Buffer, ETrackedResourceState::Storage)),
				"Manual matrix conversion requires prepared input and output buffer states.");

			const bool bAdded = native.Add({ source->Native, destination->Native, conversion });
			ASSERT(bAdded, "Reserved matrix conversion storage must admit every validated conversion.");
		}

		for (usize index = 0; index < conversions.Size(); ++index)
		{
			const rhi::CoopVecMatrixConversion& current = conversions[index];
			const auto overlaps = [&](const CoopVecMatrixLocation& a, const CoopVecMatrixLocation& b) noexcept
				{
					if (a.Buffer == b.Buffer)
					{
						return a.OffsetBytes < b.OffsetBytes + b.ByteCount && b.OffsetBytes < a.OffsetBytes + a.ByteCount;
					}

					const BufferSlot* const left = findBuffer(a.Buffer);
					const BufferSlot* const right = findBuffer(b.Buffer);

					if (!left->Heap.IsValid() || left->Heap != right->Heap)
					{
						return false;
					}

					return left->HeapOffset <= right->HeapOffset ? right->HeapOffset - left->HeapOffset < left->AllocationSize
						: left->HeapOffset - right->HeapOffset < right->AllocationSize;
				};
			ASSERT(!overlaps(current.Source, current.Destination), "Matrix conversion source and destination must not overlap.");

			for (usize other = index + 1; other < conversions.Size(); ++other)
			{
				const rhi::CoopVecMatrixConversion& next = conversions[other];

				ASSERT(!(overlaps(current.Source, next.Source) ||
						overlaps(current.Source, next.Destination) ||
						overlaps(current.Destination, next.Source) ||
						overlaps(current.Destination, next.Destination)),
					"Batched matrix conversion ranges must not overlap.");
			}
		}

		for (const CoopVecMatrixConversion& conversion : conversions)
		{
			BufferSlot& source = m_State->m_Buffers[conversion.Source.Buffer.Slot];
			BufferSlot& destination = m_State->m_Buffers[conversion.Destination.Buffer.Slot];
			(void)command->AddBufferUse({ conversion.Source.Buffer, source.Native, ETrackedResourceState::ShaderRead });
			(void)command->AddBufferUse({ conversion.Destination.Buffer, destination.Native, ETrackedResourceState::Storage });
			++source.RecordedUses;
			++destination.RecordedUses;
		}

		const Result<void> recorded = backend::RecordCoopVecMatrices(m_State->m_Backend.State, command->Native, native.View(), command->bAutomaticBarriers);
		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}
}
