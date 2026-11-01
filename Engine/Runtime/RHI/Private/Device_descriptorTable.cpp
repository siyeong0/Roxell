#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<bool> Device::isDescriptorTableVersionBusy(const DescriptorTableVersion& version) const
	{
		if (version.RecordedUses != 0)
		{
			return Result<bool>::Success(true);
		}

		if (version.LastUse == 0)
		{
			return Result<bool>::Success(false);
		}

		Result<bool> complete = backend::IsComplete(m_State->m_Backend.State, version.LastUse);

		if (!complete)
		{
			return Result<bool>::Failure(complete.GetError());
		}

		return Result<bool>::Success(!complete.Value());
	}

	void Device::releaseDescriptorEntry(const DescriptorTableEntry& entry) noexcept
	{
		if (entry.AccelerationStructure.IsValid())
		{
			--m_State->m_AccelStructs[entry.AccelerationStructure.Slot].BindingSetRefs;
			--m_State->m_AccelStructs[entry.AccelerationStructure.Slot].TableClosureRefs;
		}
		if (entry.BottomReferences)
		{
			for (rhi::AccelStructHandle const handle : *entry.BottomReferences)
			{
				--m_State->m_AccelStructs[handle.Slot].BindingSetRefs;
			}
		}

		if (entry.BufferView.IsValid())
		{
			--m_State->m_BufferViews[entry.BufferView.Slot].BindingSetRefs;
		}

		if (entry.View.IsValid())
		{
			--m_State->m_TextureViews[entry.View.Slot].BindingSetRefs;
		}

		if (entry.Texture.IsValid())
		{
			--m_State->m_Textures[entry.Texture.Slot].BindingSetRefs;
		}

		if (entry.Sampler.IsValid())
		{
			--m_State->m_Samplers[entry.Sampler.Slot].BindingSetRefs;
		}

		if (entry.Buffer.IsValid())
		{
			--m_State->m_Buffers[entry.Buffer.Slot].BindingSetRefs;
		}
	}

	[[nodiscard]] bool Device::tryRetainDescriptorEntry(const DescriptorTableEntry& entry) noexcept
	{
		constexpr uint32 MAXIMUM_REFS = NumericLimits<uint32>::max();

		if ((entry.AccelerationStructure.IsValid() &&
			 (m_State->m_AccelStructs[entry.AccelerationStructure.Slot].BindingSetRefs == MAXIMUM_REFS || m_State->m_AccelStructs[entry.AccelerationStructure.Slot].TableClosureRefs == MAXIMUM_REFS)) ||
			(entry.BufferView.IsValid() && m_State->m_BufferViews[entry.BufferView.Slot].BindingSetRefs == MAXIMUM_REFS) ||
			(entry.View.IsValid() && m_State->m_TextureViews[entry.View.Slot].BindingSetRefs == MAXIMUM_REFS) ||
			(entry.Texture.IsValid() && m_State->m_Textures[entry.Texture.Slot].BindingSetRefs == MAXIMUM_REFS) ||
			(entry.Sampler.IsValid() && m_State->m_Samplers[entry.Sampler.Slot].BindingSetRefs == MAXIMUM_REFS) ||
			(entry.Buffer.IsValid() && m_State->m_Buffers[entry.Buffer.Slot].BindingSetRefs == MAXIMUM_REFS))
		{
			return false;
		}

		if (entry.BottomReferences)
		{
			for (rhi::AccelStructHandle const handle : *entry.BottomReferences)
			{
				if (m_State->m_AccelStructs[handle.Slot].BindingSetRefs == MAXIMUM_REFS)
				{
					return false;
				}
			}
		}
		if (entry.AccelerationStructure.IsValid())
		{
			++m_State->m_AccelStructs[entry.AccelerationStructure.Slot].BindingSetRefs;
			++m_State->m_AccelStructs[entry.AccelerationStructure.Slot].TableClosureRefs;
		}

		if (entry.BottomReferences)
		{
			for (rhi::AccelStructHandle const handle : *entry.BottomReferences)
			{
				++m_State->m_AccelStructs[handle.Slot].BindingSetRefs;
			}
		}
		if (entry.BufferView.IsValid())
		{
			++m_State->m_BufferViews[entry.BufferView.Slot].BindingSetRefs;
		}

		if (entry.View.IsValid())
		{
			++m_State->m_TextureViews[entry.View.Slot].BindingSetRefs;
		}

		if (entry.Texture.IsValid())
		{
			++m_State->m_Textures[entry.Texture.Slot].BindingSetRefs;
		}

		if (entry.Sampler.IsValid())
		{
			++m_State->m_Samplers[entry.Sampler.Slot].BindingSetRefs;
		}

		if (entry.Buffer.IsValid())
		{
			++m_State->m_Buffers[entry.Buffer.Slot].BindingSetRefs;
		}

		return true;
	}

	[[nodiscard]] Result<void> Device::collectDescriptorTableVersions(DescriptorTableSlot& table)
	{
		for (usize index = 0; index < table.RetiredVersions.Size();)
		{
			DescriptorTableVersion& version = table.RetiredVersions[index];
			Result<bool> busy = isDescriptorTableVersionBusy(version);

			if (!busy)
			{
				return Result<void>::Failure(busy.GetError());
			}

			if (busy.Value())
			{
				++index;
				continue;
			}

			backend::DestroyDescriptorTable(m_State->m_Backend.State, version.Native);

			for (const DescriptorTableEntry& entry : version.Entries)
			{
				releaseDescriptorEntry(entry);
			}
			table.RetiredVersions.RemoveAtSwap(index);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] DescriptorTableVersion& Device::findDescriptorTableVersion(const RecordedDescriptorTableUse& use) noexcept
	{
		DescriptorTableSlot& table = m_State->m_DescriptorTables[use.Handle.Slot];

		if (table.Native == use.Native)
		{
			return table;
		}

		for (DescriptorTableVersion& version : table.RetiredVersions)
		{
			if (version.Native == use.Native)
			{
				return version;
			}
		}
		ASSERT(false, "A recorded descriptor version must remain alive until release.");

		return table;
	}

	[[nodiscard]] Result<void> Device::checkDescriptorTableIdle(DescriptorTableSlot& table)
	{
		Result<void> collected = collectDescriptorTableVersions(table);

		if (!collected)
		{
			return collected;
		}

		Result<bool> busy = isDescriptorTableVersionBusy(table);

		if (table.BoundReferences != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.DescriptorTable", "A binding set retains the immutable native table version.");
		}

		if (!busy)
		{
			return Result<void>::Failure(busy.GetError());
		}

		if (busy.Value() || !table.RetiredVersions.IsEmpty())
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.DescriptorTable", "A recorded or submitted command still uses a table version.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::publishDescriptorTableVersion(DescriptorTableSlot& table, const DescriptorTableDesc& desc, Array<DescriptorTableEntry>&& entries)
	{
		if (table.BoundReferences != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.DescriptorTable", "A binding set retains the immutable native table version.");
		}

		Result<void> collected = collectDescriptorTableVersions(table);

		if (!collected)
		{
			return collected;
		}

		Result<bool> busy = isDescriptorTableVersionBusy(table);

		if (!busy)
		{
			return Result<void>::Failure(busy.GetError());
		}

		if (busy.Value() && !table.RetiredVersions.Reserve(table.RetiredVersions.Size() + 1))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain a descriptor table version.");
		}

		usize retained = 0;

		for (; retained < entries.Size(); ++retained)
		{
			if (tryRetainDescriptorEntry(entries[retained]))
			{
				continue;
			}

			for (usize index = 0; index < retained; ++index)
			{
				releaseDescriptorEntry(entries[index]);
			}

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DescriptorTable", "The resources have too many descriptor references.");
		}

		Result<void*> native = createReplayedDescriptorTable(desc, entries, desc.Capacity);

		if (!native)
		{
			for (const DescriptorTableEntry& entry : entries)
			{
				releaseDescriptorEntry(entry);
			}

			return Result<void>::Failure(native.GetError());
		}

		if (busy.Value())
		{
			// The command retains the native version, including its heap range and resource references.
			const bool bAdded = table.RetiredVersions.Add(Move(static_cast<DescriptorTableVersion&>(table)));
			ASSERT(bAdded, "Descriptor version capacity must be reserved before publication.");
		}
		else
		{
			backend::DestroyDescriptorTable(m_State->m_Backend.State, table.Native);

			for (const DescriptorTableEntry& entry : table.Entries)
			{
				releaseDescriptorEntry(entry);
			}
		}
		table.Native = native.Value();
		table.Entries = Move(entries);
		table.Descriptor = desc;
		table.LastUse = 0;
		table.RecordedUses = 0;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::updateDescriptorTableEntry(DescriptorTableSlot& table, uint32 index, const DescriptorTableEntry& replacement)
	{
		if (table.BoundReferences != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.DescriptorTable", "A binding set retains the immutable native table version.");
		}

		const DescriptorTableEntry previous = table.Entries[index];

		if (previous.Texture == replacement.Texture &&
			previous.View == replacement.View &&
			previous.Sampler == replacement.Sampler &&
			previous.Buffer == replacement.Buffer &&
			previous.Range.ByteOffset == replacement.Range.ByteOffset &&
			previous.Range.ByteCount == replacement.Range.ByteCount &&
			previous.BufferView == replacement.BufferView &&
			previous.AccelerationStructure == replacement.AccelerationStructure &&
			previous.BottomReferences.Get() == replacement.BottomReferences.Get())
		{
			return Result<void>::Success();
		}

		Result<void> collected = collectDescriptorTableVersions(table);

		if (!collected)
		{
			return collected;
		}

		Result<bool> busy = isDescriptorTableVersionBusy(table);

		if (!busy)
		{
			return Result<void>::Failure(busy.GetError());
		}

		if (busy.Value())
		{
			Array<DescriptorTableEntry> entries(m_State->m_Resource);

			if (!entries.Resize(table.Entries.Size()))
			{
				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not copy descriptor entries.");
			}

			for (usize entry = 0; entry < entries.Size(); ++entry)
			{
				entries[entry] = table.Entries[entry];
			}
			entries[index] = replacement;

			return publishDescriptorTableVersion(table, table.Descriptor, Move(entries));
		}

		if (!tryRetainDescriptorEntry(replacement))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DescriptorTable", "The resources have too many descriptor references.");
		}

		Result<void> written = backend::WriteDescriptorTable(
			m_State->m_Backend.State, table.Native, index, replacement.Texture.IsValid() ? m_State->m_Textures[replacement.Texture.Slot].Native : nullptr,
			replacement.View.IsValid() ? m_State->m_TextureViews[replacement.View.Slot].Native
			: replacement.BufferView.IsValid() ? m_State->m_BufferViews[replacement.BufferView.Slot].Native
			: nullptr,
			replacement.Sampler.IsValid() ? m_State->m_Samplers[replacement.Sampler.Slot].Native : nullptr,
			replacement.Buffer.IsValid() ? m_State->m_Buffers[replacement.Buffer.Slot].Native
			: replacement.AccelerationStructure.IsValid() ? m_State->m_AccelStructs[replacement.AccelerationStructure.Slot].Native
			: nullptr,
			replacement.Range);

		if (!written)
		{
			releaseDescriptorEntry(replacement);

			return written;
		}
		releaseDescriptorEntry(previous);
		table.Entries[index] = replacement;
		table.LastUse = 0;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<DescriptorTableHandle> Device::CreateDescriptorTable(const DescriptorTableDesc& desc)
	{
		CallScope call(*this, false);

		if (m_State->m_bSubmissionFaulted)
		{
			return Result<DescriptorTableHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot create descriptors after submission failure.");
		}
		ASSERT(desc.Capacity != 0 && desc.Type <= EDescriptorType::AccelerationStructure, "A descriptor table requires a valid type and positive capacity.");
		const bool bBuffer = desc.Type == EDescriptorType::StorageBuffer || desc.Type == EDescriptorType::ReadOnlyBuffer;
		ASSERT(desc.StructureStrideBytes == 0 || (bBuffer && desc.BufferFormat == EFormat::UNKNOWN), "A structure stride requires an untyped buffer table.");
		ASSERT(desc.BufferFormat == EFormat::UNKNOWN || bBuffer, "A buffer format requires a buffer table.");
		if (desc.BufferFormat != EFormat::UNKNOWN)
		{
			Result<void> format = validateBufferBindingFormat(desc.BufferFormat, false, desc.Type == EDescriptorType::ReadOnlyBuffer);
			if (!format)
			{
				ASSERT(format.GetError().Code != EErrorCode::InvalidArgument, "A typed buffer table requires a valid uncompressed color format.");

				return Result<DescriptorTableHandle>::Failure(format.GetError());
			}
		}

		const uint32 limit = desc.Type == EDescriptorType::ReadOnlyBuffer
			? Min(m_State->m_Backend.MaximumBindlessStorageBuffers, m_State->m_Backend.MaximumBindlessSampledTextures)
			: desc.Type == EDescriptorType::SampledTexture ? m_State->m_Backend.MaximumBindlessSampledTextures
			: desc.Type == EDescriptorType::Sampler ? m_State->m_Backend.MaximumBindlessSamplers
			: (desc.Type == EDescriptorType::StorageBuffer || desc.Type == EDescriptorType::ReadOnlyBuffer)
			? m_State->m_Backend.MaximumBindlessStorageBuffers
			: desc.Type == EDescriptorType::StorageTexture ? m_State->m_Backend.MaximumBindlessStorageTextures
			: desc.Type == EDescriptorType::UniformBuffer ? m_State->m_Backend.MaximumBindlessUniformBuffers
			: desc.Type == EDescriptorType::AccelerationStructure
			? m_State->m_Backend.Capabilities.Limits.MaximumDescriptorTableAccelerationStructures
			: 0;

		if (limit == 0 || desc.Capacity > limit)
		{
			return Result<DescriptorTableHandle>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorTable", "This descriptor table type or capacity is unavailable.");
		}

		if (m_State->m_Kind == EBackend::D3D12 && desc.StructureStrideBytes > 2048)
		{
			return Result<DescriptorTableHandle>::Failure(EErrorCode::Unsupported, "Rhi.StructuredDescriptorTable", "The structure stride exceeds the backend limit.");
		}

		if (desc.BufferFormat != EFormat::UNKNOWN)
		{
			if (
				!m_State->m_Backend.Capabilities.Features.bBindlessTypedBuffers)
			{
				return Result<DescriptorTableHandle>::Failure(EErrorCode::Unsupported, "Rhi.TypedDescriptorTable", "The typed table kind or indexing feature is unavailable.");
			}
		}

		const bool bNewSlot = m_State->m_FreeDescriptorTable == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_DescriptorTables.Size()) : m_State->m_FreeDescriptorTable;

		if (bNewSlot && (m_State->m_DescriptorTables.Size() >= NumericLimits<uint32>::max() || !m_State->m_DescriptorTables.Add(DescriptorTableSlot(m_State->m_Resource))))
		{
			return Result<DescriptorTableHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a descriptor table slot.");
		}
		DescriptorTableSlot& slot = m_State->m_DescriptorTables[index];
		slot.Entries.Reset();
		auto allocationFailure = [&]() -> Result<DescriptorTableHandle>
			{
				slot.Entries.Reset();

				if (bNewSlot)
				{
					slot.NextFree = m_State->m_FreeDescriptorTable;
					m_State->m_FreeDescriptorTable = index;
				}

				return Result<DescriptorTableHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate descriptor entries.");
			};
		if (!slot.Entries.Resize(desc.Capacity))
		{
			return allocationFailure();
		}

		Result<void*> native = backend::CreateDescriptorTable(m_State->m_Backend.State, desc);

		if (!native)
		{
			slot.Entries.Reset();

			if (bNewSlot)
			{
				slot.NextFree = m_State->m_FreeDescriptorTable;
				m_State->m_FreeDescriptorTable = index;
			}

			return Result<DescriptorTableHandle>::Failure(native.GetError());
		}

		if (!bNewSlot)
		{
			m_State->m_FreeDescriptorTable = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.Native = native.Value();
		slot.Descriptor = desc;
		slot.LastUse = 0;
		slot.RecordedUses = 0;
		slot.bLive = true;

		return Result<DescriptorTableHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<uint32> Device::GetDescriptorTableDeviceHeapIndex(DescriptorTableHandle handle, uint32 entry)
	{
		CallScope call(*this, true);
		const DescriptorTableSlot* const table = findDescriptorTable(handle);

		ASSERT(table != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(entry < table->Descriptor.Capacity, "A descriptor table index must be in range.");

		if constexpr (backend::HAS_GET_DESCRIPTOR_TABLE_DEVICE_HEAP_INDEX == false)
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorTable", "Device descriptor heap indexing is unavailable.");
		}

		const uint32 first = backend::GetDescriptorTableDeviceHeapIndex(m_State->m_Backend.State, table->Native);

		if (first == NumericLimits<uint32>::max() || first > NumericLimits<uint32>::max() - entry)
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorTable", "The descriptor table has no device heap index.");
		}

		return Result<uint32>::Success(first + entry);
	}

	[[nodiscard]] Result<void> Device::CopyDescriptorTableEntries(
		DescriptorTableHandle sourceHandle,
		uint32 sourceIndex,
		DescriptorTableHandle destinationHandle,
		uint32 destinationIndex,
		uint32 count)
	{
		CallScope call(*this, false);
		const DescriptorTableSlot* const source = findDescriptorTable(sourceHandle);
		DescriptorTableSlot* const destination = findDescriptorTable(destinationHandle);
		ASSERT(source != nullptr && destination != nullptr, "RHI operations require valid handles for this device.");
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot mutate descriptors after submission failure.");
		}
		ASSERT(source != destination &&
			count != 0 &&
			sourceIndex < source->Descriptor.Capacity &&
			count <= source->Descriptor.Capacity - sourceIndex &&
			destinationIndex < destination->Descriptor.Capacity &&
			count <= destination->Descriptor.Capacity - destinationIndex &&
			source->Descriptor.Type == destination->Descriptor.Type &&
			source->Descriptor.BufferFormat == destination->Descriptor.BufferFormat &&
			source->Descriptor.StructureStrideBytes == destination->Descriptor.StructureStrideBytes,
			"Descriptor copy requires matching tables and valid nonempty ranges.");
		Result<void> idle = checkDescriptorTableIdle(*destination);
		if (!idle)
		{
			return idle;
		}
		for (uint32 index = 0; index < count; ++index)
		{
			rhi::DescriptorTableEntry const& entry = source->Entries[sourceIndex + index];
			ASSERT(entry.Texture.IsValid() || entry.Sampler.IsValid() || entry.Buffer.IsValid() || entry.AccelerationStructure.IsValid(), "Descriptor copy cannot read an empty source entry.");
		}
		uint32 retained = 0;
		for (; retained < count; ++retained)
		{
			if (tryRetainDescriptorEntry(source->Entries[sourceIndex + retained]))
			{
				continue;
			}
			for (uint32 index = 0; index < retained; ++index)
			{
				releaseDescriptorEntry(source->Entries[sourceIndex + index]);
			}

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DescriptorCopy", "The resources have too many descriptor references.");
		}

		Result<void> copied = backend::CopyDescriptorTableEntries(m_State->m_Backend.State, source->Native, sourceIndex, destination->Native, destinationIndex, count);
		if (!copied)
		{
			for (uint32 index = 0; index < count; ++index)
			{
				releaseDescriptorEntry(source->Entries[sourceIndex + index]);
			}

			return copied;
		}
		for (uint32 index = 0; index < count; ++index)
		{
			releaseDescriptorEntry(destination->Entries[destinationIndex + index]);
			destination->Entries[destinationIndex + index] = source->Entries[sourceIndex + index];
		}
		destination->LastUse = 0;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::WriteDescriptorTable(DescriptorTableHandle handle, const DescriptorTableWrite& write)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot mutate descriptors after submission failure.");
		}
		DescriptorTableSlot* const table = findDescriptorTable(handle);
		ASSERT(table != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(write.Index < table->Descriptor.Capacity, "A descriptor table index must be in range.");
		const EDescriptorValueKind kind = write.Value.GetKind();
		const EDescriptorType type = table->Descriptor.Type;
		ASSERT(kind != EDescriptorValueKind::Invalid, "A descriptor table write requires a value.");

		if (type == EDescriptorType::AccelerationStructure)
		{
			ASSERT(kind == EDescriptorValueKind::AccelerationStructure, "A descriptor value must match the table type.");
			const AccelStructHandle accelerationStructure = write.Value.GetAccelerationStructure();
			const AccelStructSlot* const accel = findAccelStruct(accelerationStructure);
			ASSERT(accel != nullptr && accel->Type == EAccelStructType::TopLevel, "RHI operations require valid handles for this device.");
			ASSERT(accel->bBuilt, "An acceleration structure descriptor requires a built top-level structure.");
			if (accel->bUnavailable || accel->RecordedUses != 0 || accel->PreparedReferences != 0)
			{
				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.DescriptorTable", "The acceleration structure is unavailable or retained by recording.");
			}
			DescriptorTableEntry replacement;
			replacement.AccelerationStructure = accelerationStructure;
			const Array<AccelStructHandle>& children = accel->PreparedBottomReferences.IsEmpty() ? accel->BottomReferences : accel->PreparedBottomReferences;
			if (!children.IsEmpty())
			{
				if (!MakeSharedInto(m_State->m_Resource, &replacement.BottomReferences, m_State->m_Resource) || !replacement.BottomReferences->Reserve(children.Size()))
				{
					return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not capture descriptor acceleration structure dependencies.");
				}
				for (const AccelStructHandle child : children)
				{
					const
						AccelStructSlot* const bottom = findAccelStruct(child);
					ASSERT(bottom != nullptr && bottom->Type == EAccelStructType::BottomLevel && bottom->bBuilt, "A built top-level structure retains built bottom-level structures.");
					if (bottom->bUnavailable)
					{
						return Result<void>::Failure(EErrorCode::Conflict, "Rhi.DescriptorTable", "A referenced bottom-level structure is unavailable.");
					}
					replacement.BottomReferences->AddRequired(child);
				}
			}

			return updateDescriptorTableEntry(*table, write.Index, replacement);
		}

		if (type == EDescriptorType::UniformBuffer || type == EDescriptorType::StorageBuffer || type == EDescriptorType::ReadOnlyBuffer)
		{
			const bool bUniform = type == EDescriptorType::UniformBuffer;
			const bool bReadOnly = type == EDescriptorType::ReadOnlyBuffer; const bool bTyped = table->Descriptor.BufferFormat != EFormat::UNKNOWN;
			ASSERT(kind == EDescriptorValueKind::Buffer || (!bUniform && kind == EDescriptorValueKind::BufferView), "A descriptor value must match the table type.");

			const BufferViewHandle viewHandle = kind == EDescriptorValueKind::BufferView ? write.Value.GetBufferView() : BufferViewHandle{};
			const BufferViewSlot* const view = viewHandle.IsValid() ? findBufferView(viewHandle) : nullptr;

			ASSERT(!viewHandle.IsValid() || view != nullptr, "RHI operations require valid handles for this device.");
			ASSERT(bTyped == (view != nullptr) && (view == nullptr || (view->Descriptor.Format == table->Descriptor.BufferFormat && (view->Descriptor.Type == EBufferViewType::ReadOnly) == bReadOnly)),
				"A buffer view must match the table format and access.");
			const BufferHandle bufferHandle = view != nullptr ? view->Descriptor.Buffer : write.Value.GetBuffer();
			const BufferSlot* const buffer = findBuffer(bufferHandle);
			ASSERT(buffer != nullptr, "RHI operations require valid handles for this device."); BufferRange range =
				view != nullptr
				? view->Descriptor.Range
				: write.Value.GetBufferRange();
			if (range.ByteOffset == 0 && range.ByteCount == 0) {
				range.ByteCount = buffer->ByteCount;
			}
			const uint64 alignmentBytes = bUniform ? m_State->m_Backend.Capabilities.Limits.UniformBufferOffsetAlignmentBytes : m_State->m_Backend.Capabilities.Limits.StorageBufferOffsetAlignmentBytes;
			const uint32 strideBytes = table->Descriptor.StructureStrideBytes;
			const uint32 unitBytes = bUniform ? 256u : strideBytes != 0 ? strideBytes : 4u;
			ASSERT(HasUsage(buffer->Usage, bUniform ? EBufferUsage::Uniform : EBufferUsage::Storage) &&
				!buffer->bMapped &&
				range.ByteCount != 0 &&
				Fits (buffer->ByteCount, range.ByteOffset, range.ByteCount),
				"A buffer descriptor requires compatible usage and a nonempty unmapped range.");
			ASSERT
			(bTyped ||
				(range.ByteCount % unitBytes == 0 &&
						range.ByteOffset % unitBytes == 0 &&
						alignmentBytes != 0 &&
						range.ByteOffset % alignmentBytes == 0 &&
						(bUniform ? range.ByteCount <= 65536 : range.ByteCount / unitBytes <= UINT32_MAX)),
				"A buffer descriptor range must satisfy its alignment and element limits.");

			return updateDescriptorTableEntry(*table, write.Index, { {}, {}, {}, bufferHandle, range, viewHandle });
		}

		if (type == EDescriptorType::Sampler)
		{
			ASSERT(kind == EDescriptorValueKind::Sampler, "A descriptor value must match the table type."); const SamplerHandle samplerHandle = write.Value.GetSampler();

			ASSERT(findSampler(samplerHandle) != nullptr, "RHI operations require valid handles for this device.");

			return updateDescriptorTableEntry(*table, write.Index, { {}, {}, samplerHandle });
		}

		const bool bStorage = type == EDescriptorType::StorageTexture;
		ASSERT((bStorage && (kind == EDescriptorValueKind::StorageTexture || kind == EDescriptorValueKind::StorageView)) ||
			(!bStorage && (kind == EDescriptorValueKind::SampledTexture || kind == EDescriptorValueKind::SampledView)),
			"A descriptor value must match the table type.");
		const bool bView = kind == EDescriptorValueKind::SampledView || kind == EDescriptorValueKind::StorageView;
		const TextureViewHandle viewHandle = bView ? write.Value.GetTextureView() : TextureViewHandle
		{
		};
		const
			TextureViewSlot* const view = bView ? findTextureView(viewHandle) : nullptr;

		ASSERT(!bView || view != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(view == nullptr || view->Descriptor.Type == (bStorage ? ETextureViewType::Storage : ETextureViewType::Sampled), "A texture view must match the table access.");

		const TextureHandle textureHandle = view != nullptr ? view->Descriptor.Texture : write.Value.GetTexture();
		const
			TextureSlot* const texture = findTexture(textureHandle);

		ASSERT(texture != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(HasUsage(texture->Descriptor.Usage, bStorage ? ETextureUsage::Storage : ETextureUsage::ShaderRead), "A texture descriptor requires compatible resource usage.");

		return updateDescriptorTableEntry(*table, write.Index, { textureHandle, viewHandle });
	}

	[[nodiscard]] Result<void*> Device::createReplayedDescriptorTable(const DescriptorTableDesc& desc, const Array<DescriptorTableEntry>& entries, uint32 copied)
	{
		Result<void*> native = backend::CreateDescriptorTable(m_State->m_Backend.State, desc);

		if (!native)
		{
			return native;
		}

		for (uint32 index = 0; index < copied; ++index)
		{
			const DescriptorTableEntry& entry = entries[index];

			if (!entry.Texture.IsValid() && !entry.Sampler.IsValid() && !entry.Buffer.IsValid() && !entry.AccelerationStructure.IsValid())
			{
				continue;
			}
			TextureSlot* const texture = entry.Texture.IsValid() ? findTexture(entry.Texture, true) : nullptr;
			TextureViewSlot* const view = entry.View.IsValid() ? findTextureView(entry.View) : nullptr;
			SamplerSlot* const sampler = entry.Sampler.IsValid() ? findSampler(entry.Sampler) : nullptr;
			BufferSlot* const buffer = entry.Buffer.IsValid() ? findBuffer(entry.Buffer, true) : nullptr;
			ASSERT((!entry.Texture.IsValid() || texture != nullptr) &&
				(!entry.View.IsValid() || view != nullptr) &&
				(!entry.Sampler.IsValid() || sampler != nullptr) &&
				(!entry.Buffer.IsValid() || buffer != nullptr),
				"A live descriptor table must retain its resources.");
			Result<void> written = backend::WriteDescriptorTable(
				m_State->m_Backend.State, native.Value(), index, texture != nullptr ? texture->Native : nullptr,
				view != nullptr ? view->Native
				: entry.BufferView.IsValid() ? m_State->m_BufferViews[entry.BufferView.Slot].Native
				: nullptr,
				sampler != nullptr ? sampler->Native : nullptr,
				buffer != nullptr ? buffer->Native
				: entry.AccelerationStructure.IsValid() ? m_State->m_AccelStructs[entry.AccelerationStructure.Slot].Native
				: nullptr,
				entry.Range);

			if (!written)
			{
				backend::DestroyDescriptorTable(m_State->m_Backend.State, native.Value());

				return Result<void*>::Failure(written.GetError());
			}
		}

		return native;
	}

	[[nodiscard]] Result<void> Device::ClearDescriptorTableEntry(DescriptorTableHandle handle, uint32 index)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot mutate descriptors after submission failure.");
		}
		DescriptorTableSlot* const table = findDescriptorTable(handle);

		ASSERT(table != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(index < table->Descriptor.Capacity, "A descriptor table index must be in range.");

		return updateDescriptorTableEntry(*table, index, {});
	}

	[[nodiscard]] Result<void> Device::ResizeDescriptorTable(DescriptorTableHandle handle, uint32 capacity, bool bKeepContents)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot mutate descriptors after submission failure.");
		}
		DescriptorTableSlot* const table = findDescriptorTable(handle);

		ASSERT(table != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(capacity != 0, "A descriptor table capacity must be positive.");

		const uint32 limit =
			table->Descriptor.Type == EDescriptorType::ReadOnlyBuffer
			? Min(m_State->m_Backend.MaximumBindlessStorageBuffers, m_State->m_Backend.MaximumBindlessSampledTextures)
			: table->Descriptor.Type == EDescriptorType::Sampler ? m_State->m_Backend.MaximumBindlessSamplers
			: (table->Descriptor.Type == EDescriptorType::StorageBuffer || table->Descriptor.Type == EDescriptorType::ReadOnlyBuffer)
			? m_State->m_Backend.MaximumBindlessStorageBuffers
			: table->Descriptor.Type == EDescriptorType::StorageTexture ? m_State->m_Backend.MaximumBindlessStorageTextures
			: table->Descriptor.Type == EDescriptorType::UniformBuffer ? m_State->m_Backend.MaximumBindlessUniformBuffers
			: table->Descriptor.Type == EDescriptorType::AccelerationStructure
			? m_State->m_Backend.Capabilities.Limits.MaximumDescriptorTableAccelerationStructures
			: m_State->m_Backend.MaximumBindlessSampledTextures;

		if (capacity > limit)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorTable", "The capacity exceeds the device limit.");
		}

		if (capacity == table->Descriptor.Capacity && bKeepContents)
		{
			return Result<void>::Success();
		}

		Array<DescriptorTableEntry> replacement(m_State->m_Resource);

		if (!replacement.Resize(capacity))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate resized descriptor entries.");
		}

		const uint32 copied = bKeepContents ? (capacity < table->Descriptor.Capacity ? capacity : table->Descriptor.Capacity) : 0;

		for (uint32 index = 0; index < copied; ++index)
		{
			replacement[index] = table->Entries[index];
		}

		const DescriptorTableDesc resizedDesc{ table->Descriptor.Type, capacity, table->Descriptor.BufferFormat,
											  table->Descriptor.StructureStrideBytes };

		return publishDescriptorTableVersion(*table, resizedDesc, Move(replacement));
	}

	[[nodiscard]] Result<void> Device::DestroyDescriptorTable(DescriptorTableHandle handle)
	{
		CallScope call(*this, false);
		DescriptorTableSlot* const table = findDescriptorTable(handle);

		ASSERT(table != nullptr, "RHI operations require valid handles for this device.");

		Result<void> idle = checkDescriptorTableIdle(*table);

		if (!idle)
		{
			return idle;
		}

		backend::DestroyDescriptorTable(m_State->m_Backend.State, table->Native);
		table->Native = nullptr;

		for (const DescriptorTableEntry& entry : table->Entries)
		{
			releaseDescriptorEntry(entry);
		}
		table->Entries.Reset();
		table->bLive = false;
		Advance(*table);

		if (!table->bExhausted)
		{
			table->NextFree = m_State->m_FreeDescriptorTable;
			m_State->m_FreeDescriptorTable = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] DescriptorTableSlot* Device::findDescriptorTable(DescriptorTableHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_DescriptorTables.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		DescriptorTableSlot& slot = m_State->m_DescriptorTables[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveDescriptorTable(
		uint32 capacity,
		EDescriptorType type,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot,
		EFormat format)
	{
		if (capacity == 0)
		{
			ASSERT(!handle.IsValid(), "The pipeline has no descriptor table layout.");

			return Result<DescriptorTableSlot*>::Success(nullptr);
		}
		DescriptorTableSlot* const table = findDescriptorTable(handle);

		ASSERT(table != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(table->Descriptor.Type == type && table->Descriptor.Capacity == capacity && table->Descriptor.BufferFormat == format, "The table does not match the pipeline.");

		if (!command.DescriptorTableUses.Reserve(command.DescriptorTableUses.Size() + 1) ||
			((type == EDescriptorType::SampledTexture || type == EDescriptorType::StorageTexture) && !command.TextureUses.Reserve(command.TextureUses.Size() + table->Entries.Size())) ||
			(type == EDescriptorType::Sampler && !command.SamplerUses.Reserve(command.SamplerUses.Size() + table->Entries.Size())) ||
			((type == EDescriptorType::StorageBuffer || type == EDescriptorType::ReadOnlyBuffer || type == EDescriptorType::UniformBuffer) &&
			 !command.BufferUses.Reserve(command.BufferUses.Size() + table->Entries.Size())))
		{
			return Result<DescriptorTableSlot*>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track descriptor table resources.");
		}

		for (const DescriptorTableEntry& entry : table->Entries)
		{
			if (entry.Buffer.IsValid())
			{
				BufferSlot* const buffer = findBuffer(entry.Buffer);

				ASSERT(buffer != nullptr, "RHI operations require valid handles for this device.");

				ASSERT(!buffer->bMapped, "A table buffer is mapped.");
			}

			if (!entry.Texture.IsValid())
			{
				continue;
			}
			TextureSlot* const texture = findTexture(entry.Texture);

			ASSERT(texture != nullptr, "RHI operations require valid handles for this device.");

			if (type == EDescriptorType::SampledTexture ? !canReadTexture(*texture, listSlot) : (texture->RecordingSlot != UINT32_MAX && texture->RecordingSlot != listSlot))
			{
				return Result<DescriptorTableSlot*>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list uses a table texture.");
			}
		}

		return Result<DescriptorTableSlot*>::Success(table);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveComputeDescriptorTable(
		const PipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessSampledTextureCapacity, EDescriptorType::SampledTexture, handle, command, listSlot);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveComputeSamplerTable(const PipelineSlot& pipeline, DescriptorTableHandle handle, CommandSlot& command, uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessSamplerCapacity, EDescriptorType::Sampler, handle, command, listSlot);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveComputeReadOnlyBufferTable(
		const PipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessReadOnlyBufferCapacity, EDescriptorType::ReadOnlyBuffer, handle, command, listSlot,
			pipeline.BindlessReadOnlyBufferFormat);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveComputeStorageBufferTable(
		const PipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessStorageBufferCapacity, EDescriptorType::StorageBuffer, handle, command, listSlot,
			pipeline.BindlessStorageBufferFormat);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveComputeStorageTextureTable(
		const PipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessStorageTextureCapacity, EDescriptorType::StorageTexture, handle, command, listSlot);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveComputeUniformBufferTable(
		const PipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessUniformBufferCapacity, EDescriptorType::UniformBuffer, handle, command, listSlot);
	}

	[[nodiscard]] Result<void> Device::checkStorageTableTextureAliases(const DescriptorTableSlot* storage, const DescriptorTableSlot* sampled)
	{
		if (storage == nullptr || sampled == nullptr)
		{
			return Result<void>::Success();
		}
		++m_State->m_AliasEpoch;

		if (m_State->m_AliasEpoch == 0)
		{
			for (TextureSlot& texture : m_State->m_Textures)
			{
				texture.AliasEpoch = 0;
			}
			++m_State->m_AliasEpoch;
		}

		for (const DescriptorTableEntry& input : sampled->Entries)
		{
			if (input.Texture.IsValid())
			{
				m_State->m_Textures[input.Texture.Slot].AliasEpoch = m_State->m_AliasEpoch;
			}
		}

		for (const DescriptorTableEntry& output : storage->Entries)
		{
			ASSERT(!output.Texture.IsValid() || m_State->m_Textures[output.Texture.Slot].AliasEpoch != m_State->m_AliasEpoch, "A texture is in sampled and storage tables at the same time.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::checkGraphicsFixedTextureTableAliases(
		const DescriptorTableSlot* sampled,
		const DescriptorTableSlot* storage,
		Span<const TextureHandle> fixedSampled,
		Span<const TextureHandle> fixedStorage)
	{
		if (sampled == nullptr && storage == nullptr)
		{
			return Result<void>::Success();
		}
		++m_State->m_AliasEpoch;

		if (m_State->m_AliasEpoch == 0)
		{
			for (TextureSlot& texture : m_State->m_Textures)
			{
				texture.AliasEpoch = 0;
			}
			++m_State->m_AliasEpoch;
		}

		for (const TextureHandle handle : fixedStorage)
		{
			m_State->m_Textures[handle.Slot].AliasEpoch = m_State->m_AliasEpoch;
		}

		if (sampled != nullptr)
		{
			for (const DescriptorTableEntry& entry : sampled->Entries)
			{
				ASSERT(!entry.Texture.IsValid() || m_State->m_Textures[entry.Texture.Slot].AliasEpoch != m_State->m_AliasEpoch, "A fixed storage texture is in the sampled table.");
			}
		}

		for (const TextureHandle handle : fixedSampled)
		{
			m_State->m_Textures[handle.Slot].AliasEpoch = m_State->m_AliasEpoch;
		}

		if (storage != nullptr)
		{
			for (const DescriptorTableEntry& entry : storage->Entries)
			{
				ASSERT(!entry.Texture.IsValid() || m_State->m_Textures[entry.Texture.Slot].AliasEpoch != m_State->m_AliasEpoch, "A fixed texture is in the storage table.");
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::checkBufferTableAliases(const DescriptorTableSlot* table, Span<const BufferHandle> fixed, BufferHandle arguments) const
	{
		if (table == nullptr)
		{
			return Result<void>::Success();
		}

		for (const DescriptorTableEntry& entry : table->Entries)
		{
			if (!entry.Buffer.IsValid())
			{
				continue;
			}

			ASSERT(entry.Buffer != arguments, "An indirect argument buffer is also in a descriptor table.");

			for (BufferHandle handle : fixed)
			{
				ASSERT(entry.Buffer != handle, "A fixed binding is also in a descriptor table.");
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::checkBufferTableOverlap(const DescriptorTableSlot* storage, const DescriptorTableSlot* uniform)
	{
		if (storage == nullptr || uniform == nullptr)
		{
			return Result<void>::Success();
		}
		++m_State->m_BufferAliasEpoch;

		if (m_State->m_BufferAliasEpoch == 0)
		{
			for (BufferSlot& buffer : m_State->m_Buffers)
			{
				buffer.AliasEpoch = 0;
			}
			++m_State->m_BufferAliasEpoch;
		}

		for (const DescriptorTableEntry& entry : storage->Entries)
		{
			if (entry.Buffer.IsValid())
			{
				m_State->m_Buffers[entry.Buffer.Slot].AliasEpoch = m_State->m_BufferAliasEpoch;
			}
		}

		for (const DescriptorTableEntry& entry : uniform->Entries)
		{
			ASSERT(!entry.Buffer.IsValid() || m_State->m_Buffers[entry.Buffer.Slot].AliasEpoch != m_State->m_BufferAliasEpoch, "A buffer is in storage and uniform tables at the same time.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveGraphicsTextureTable(
		uint32 capacity,
		EDescriptorType type,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		Result<DescriptorTableSlot*> resolved = resolveDescriptorTable(capacity, type, handle, command, listSlot);

		if (!resolved || resolved.Value() == nullptr)
		{
			return resolved;
		}

		for (const DescriptorTableEntry& entry : resolved.Value()->Entries)
		{
			if (!entry.Texture.IsValid())
			{
				continue;
			}

			for (uint32 color = 0; color < command.ColorCount; ++color)
			{
				ASSERT(entry.Texture != command.ColorTargets[color], "The table contains an active color attachment.");
			}

			ASSERT(entry.Texture != command.DepthTarget || (command.bReadOnlyDepth && type == EDescriptorType::SampledTexture), "The table contains the active depth attachment.");
		}

		return resolved;
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveGraphicsDescriptorTable(
		const GraphicsPipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveGraphicsTextureTable(pipeline.BindlessSampledTextureCapacity, EDescriptorType::SampledTexture, handle, command,
			listSlot);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveGraphicsStorageTextureTable(
		const GraphicsPipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveGraphicsTextureTable(pipeline.BindlessStorageTextureCapacity, EDescriptorType::StorageTexture, handle, command,
			listSlot);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveGraphicsSamplerTable(
		const GraphicsPipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessSamplerCapacity, EDescriptorType::Sampler, handle, command, listSlot);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveGraphicsUniformBufferTable(
		const GraphicsPipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessUniformBufferCapacity, EDescriptorType::UniformBuffer, handle, command, listSlot);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveGraphicsReadOnlyBufferTable(
		const GraphicsPipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessReadOnlyBufferCapacity, EDescriptorType::ReadOnlyBuffer, handle, command, listSlot,
			pipeline.BindlessReadOnlyBufferFormat);
	}

	[[nodiscard]] Result<DescriptorTableSlot*> Device::resolveGraphicsStorageBufferTable(
		const GraphicsPipelineSlot& pipeline,
		DescriptorTableHandle handle,
		CommandSlot& command,
		uint32 listSlot)
	{
		if (pipeline.bHeapDirectlyIndexed && !pipeline.bDeviceHeapIndexed && !handle.IsValid())
		{
			return Result<DescriptorTableSlot*>::Success(nullptr);
		}

		return resolveDescriptorTable(pipeline.BindlessStorageBufferCapacity, EDescriptorType::StorageBuffer, handle, command, listSlot,
			pipeline.BindlessStorageBufferFormat);
	}

	[[nodiscard]] Result<void> Device::checkGraphicsBufferTableAliases(
		const DescriptorTableSlot* table,
		Span<const BufferHandle> fixed,
		BufferHandle vertex,
		BufferHandle index,
		BufferHandle arguments,
		BufferHandle count) const
	{
		Result<void> fixedAliases = checkBufferTableAliases(table, fixed, arguments);

		if (!fixedAliases || table == nullptr)
		{
			return fixedAliases;
		}

		for (const DescriptorTableEntry& entry : table->Entries)
		{
			if (!entry.Buffer.IsValid())
			{
				continue;
			}

			ASSERT(entry.Buffer != vertex && entry.Buffer != index && entry.Buffer != count, "A vertex, index, or count buffer is also in a descriptor table.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] bool Device::reserveBindingTableUses(
		CommandSlot& command,
		const DescriptorTableSlot* sampled,
		const DescriptorTableSlot* samplers,
		const DescriptorTableSlot* storage,
		const DescriptorTableSlot* storageTextures,
		const DescriptorTableSlot* uniform,
		const DescriptorTableSlot* readOnly,
		usize fixedTextures,
		usize fixedBuffers,
		BindingSetHandle bindingSet)
	{
		const BindingSetSlot* const set = findBindingSet(bindingSet);
		const usize tableCount = (sampled != nullptr ? 1 : 0) + (samplers != nullptr ? 1 : 0) + (storage != nullptr ? 1 : 0) +
			(storageTextures != nullptr ? 1 : 0) + (uniform != nullptr ? 1 : 0) +
			(readOnly != nullptr ? 1 : 0);

		return command.DescriptorTableUses.Reserve(command.DescriptorTableUses.Size() + tableCount) &&
			command.TextureUses.Reserve(command.TextureUses.Size() + fixedTextures + (sampled != nullptr ? sampled->Entries.Size() : 0) + (storageTextures != nullptr ? storageTextures->Entries.Size() : 0)) &&
			command.SamplerUses.Reserve(command.SamplerUses.Size() + (set != nullptr ? set->SamplerCount : 0) + (samplers != nullptr ? samplers->Entries.Size() : 0)) &&
			command.BufferUses.Reserve(command.BufferUses.Size() + fixedBuffers + (storage != nullptr ? storage->Entries.Size() : 0) + (uniform != nullptr ? uniform->Entries.Size() : 0) + (readOnly != nullptr ? readOnly->Entries.Size() : 0));
	}

	void Device::trackDescriptorTableUse(CommandSlot& command, uint32 listSlot, DescriptorTableHandle handle, DescriptorTableSlot* table) noexcept
	{
		if (table == nullptr)
		{
			return;
		}

		const bool bTableAdded = command.DescriptorTableUses.Add({ handle, table->Native });
		ASSERT(bTableAdded, "Descriptor use capacity must be reserved before recording.");
		++table->RecordedUses;

		for (const DescriptorTableEntry& entry : table->Entries)
		{
			if (entry.Buffer.IsValid())
			{
				BufferSlot& buffer = m_State->m_Buffers[entry.Buffer.Slot];
				const bool bAdded = command.AddBufferUse({ entry.Buffer, buffer.Native, BufferUseState(table->Descriptor.Type) });
				ASSERT(bAdded, "Descriptor use capacity must be reserved before recording.");
				++buffer.RecordedUses;
				continue;
			}

			if (entry.Sampler.IsValid())
			{
				SamplerSlot& sampler = m_State->m_Samplers[entry.Sampler.Slot];
				const bool bAdded = command.SamplerUses.Add(entry.Sampler);
				ASSERT(bAdded, "Descriptor use capacity must be reserved before recording.");
				++sampler.RecordedUses;
				continue;
			}

			if (!entry.Texture.IsValid())
			{
				continue;
			}
			TextureSlot& texture = m_State->m_Textures[entry.Texture.Slot];
			trackTextureReadOrWrite(command, listSlot, entry.Texture, texture,
				table->Descriptor.Type == EDescriptorType::SampledTexture ? ETrackedResourceState::ShaderRead
				: ETrackedResourceState::Storage);
		}
	}
}
