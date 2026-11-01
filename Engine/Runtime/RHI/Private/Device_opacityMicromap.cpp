#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<OpacityMicromapBuildSizes> Device::GetOpacityMicromapBuildSizes(Span<const OpacityMicromapUsageCount> counts, EOpacityMicromapBuildFlags flags) const
	{
		CallScope call(*this, true);

		if (!m_State->m_Backend.bOpacityMicromap)
		{
			return Result<OpacityMicromapBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.OpacityMicromap", "The device does not support opacity micromaps.");
		}

		ASSERT(!(counts.IsEmpty() ||
				counts.Size() > NumericLimits<uint32>::max() ||
				(static_cast<uint8>(flags) & ~uint8{ 7 }) != 0 ||
				(static_cast<uint8>(flags) & uint8{ 3 }) == uint8{ 3 }),
			"Micromap size queries require nonempty bounded usage counts and compatible flags.");

		uint64 total = 0;

		for (const OpacityMicromapUsageCount& count : counts)
		{
			ASSERT(!(count.Count == 0 ||
					(count.Format != EOpacityMicromapFormat::TwoState && count.Format != EOpacityMicromapFormat::FourState) ||
					total > NumericLimits<uint32>::max() - count.Count),
				"Micromap usage counts require valid formats and a nonzero bounded triangle count.");
			total += count.Count;
		}

		return backend::GetOpacityMicromapBuildSizes(m_State->m_Backend.State, counts, flags);
	}

	[[nodiscard]] Result<OpacityMicromapHandle> Device::CreateOpacityMicromap(const OpacityMicromapDesc& desc)
	{
		CallScope call(*this, false);

		if (!m_State->m_Backend.bOpacityMicromap)
		{
			return Result<OpacityMicromapHandle>::Failure(EErrorCode::Unsupported, "Rhi.OpacityMicromap", "The device does not support opacity micromaps.");
		}
		BufferSlot* const storage = findBuffer(desc.Storage);

		ASSERT(storage != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(!HasUsage(storage->Usage, EBufferUsage::MicromapStorage) ||
				desc.ByteOffset % 256 != 0 ||
				desc.ByteCount == 0 ||
				desc.ByteOffset > storage->ByteCount ||
				desc.ByteCount > storage->ByteCount - desc.ByteOffset),
			"Micromap storage requires compatible usage and a nonempty contained aligned range.");

		for (const OpacityMicromapSlot& other : m_State->m_OpacityMicromaps)
		{
			if (other.bLive && other.Storage == desc.Storage && desc.ByteOffset < other.ByteOffset + other.ByteCount && other.ByteOffset < desc.ByteOffset + desc.ByteCount)
			{
				return Result<OpacityMicromapHandle>::Failure(EErrorCode::Conflict, "Rhi.OpacityMicromapStorage", "Another micromap occupies the storage range.");
			}
		}

		if (storage->BindingSetRefs == NumericLimits<uint32>::max())
		{
			return Result<OpacityMicromapHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.OpacityMicromapStorage", "The storage buffer has too many references.");
		}

		const bool bNewSlot = m_State->m_FreeOpacityMicromap == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_OpacityMicromaps.Size()) : m_State->m_FreeOpacityMicromap;

		if (bNewSlot && (m_State->m_OpacityMicromaps.Size() >= NumericLimits<uint32>::max() || !m_State->m_OpacityMicromaps.Add(OpacityMicromapSlot{})))
		{
			return Result<OpacityMicromapHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate an opacity micromap handle.");
		}

		Result<void*> created = backend::CreateOpacityMicromap(m_State->m_Backend.State, desc, storage->Native);

		if (!created)
		{
			if (bNewSlot)
			{
				m_State->m_OpacityMicromaps[index].NextFree = m_State->m_FreeOpacityMicromap;
				m_State->m_FreeOpacityMicromap = index;
			}

			return Result<OpacityMicromapHandle>::Failure(created.GetError());
		}
		OpacityMicromapSlot& slot = m_State->m_OpacityMicromaps[index];

		if (!bNewSlot)
		{
			m_State->m_FreeOpacityMicromap = slot.NextFree;
		}
		slot.Native = created.Value();
		slot.Storage = desc.Storage;
		slot.ByteOffset = desc.ByteOffset;
		slot.ByteCount = desc.ByteCount;
		slot.bBuilt = false;
		slot.bLive = true;
		slot.NextFree = NumericLimits<uint32>::max();
		++storage->BindingSetRefs;

		return Result<OpacityMicromapHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyOpacityMicromap(OpacityMicromapHandle handle)
	{
		CallScope call(*this, false);
		OpacityMicromapSlot* const slot = findOpacityMicromap(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->RecordedUses != 0 || slot->AccelStructRefs != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.OpacityMicromap", "The micromap is referenced by a command list or acceleration structure.");
		}

		Result<void> destroyed = backend::DestroyOpacityMicromap(m_State->m_Backend.State, slot->Native, m_State->m_Buffers[slot->Storage.Slot].Native);

		if (!destroyed)
		{
			return destroyed;
		}
		--m_State->m_Buffers[slot->Storage.Slot].BindingSetRefs;
		slot->Native = nullptr;
		slot->Storage = {};
		slot->ByteOffset = 0;
		slot->ByteCount = 0;
		slot->bBuilt = false;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeOpacityMicromap;
			m_State->m_FreeOpacityMicromap = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::ReferenceOpacityMicromap(CommandListHandle list, OpacityMicromapHandle handle)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		OpacityMicromapSlot* const slot = findOpacityMicromap(handle);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		if (!command->OpacityMicromapUses.Reserve(command->OpacityMicromapUses.Size() + 1))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.OpacityMicromap", "Could not retain a micromap reference.");
		}

		Result<void> retained = ReferenceBuffer(list, slot->Storage);

		if (!retained)
		{
			return retained;
		}
		const bool bAdded = command->OpacityMicromapUses.Add(handle);
		ASSERT(bAdded, "Reserved micromap reference storage must admit the validated reference.");
		++slot->RecordedUses;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BuildOpacityMicromap(
		CommandListHandle list,
		OpacityMicromapHandle target,
		const OpacityMicromapBuildDesc& desc,
		BufferHandle scratch,
		uint64 scratchOffset)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		ASSERT(command != nullptr, "Micromap builds require a live command list.");
		ASSERT(command->Queue != EQueue::Copy && !command->bRendering, "Micromap builds require a graphics or compute list outside rendering.");

		if (!m_State->m_Backend.bOpacityMicromap)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.OpacityMicromap", "The device does not support opacity micromaps.");
		}
		OpacityMicromapSlot* const micromap = findOpacityMicromap(target);
		BufferSlot* const input = findBuffer(desc.InputData);
		BufferSlot* const triangles = findBuffer(desc.TriangleArray);
		BufferSlot* const scratchBuffer = findBuffer(scratch);

		ASSERT(!(command == nullptr || micromap == nullptr || input == nullptr || triangles == nullptr || scratchBuffer == nullptr), "RHI operations require valid handles for this device.");

		usize ownUses = 0;

		for (rhi::OpacityMicromapHandle handle : command->OpacityMicromapUses)
		{
			ownUses += handle == target ? 1u : 0u;
		}

		if (micromap->AccelStructRefs != 0 || micromap->RecordedUses != ownUses)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.OpacityMicromap", "A referenced micromap cannot be rebuilt while its acceleration structures remain live.");
		}
		BufferSlot& storage = m_State->m_Buffers[micromap->Storage.Slot];

		ASSERT(!(desc.InputData == micromap->Storage ||
				desc.TriangleArray == micromap->Storage ||
				scratch == micromap->Storage ||
				scratch == desc.InputData ||
				scratch == desc.TriangleArray ||
				input->Memory != EBufferMemory::Device ||
				triangles->Memory != EBufferMemory::Device ||
				scratchBuffer->Memory != EBufferMemory::Device ||
				input->bMapped ||
				triangles->bMapped ||
				scratchBuffer->bMapped ||
				!HasUsage(input->Usage, EBufferUsage::MicromapBuildInput) ||
				!HasUsage(triangles->Usage, EBufferUsage::MicromapBuildInput) ||
				!HasUsage(scratchBuffer->Usage, EBufferUsage::Storage) ||
				!HasUsage(scratchBuffer->Usage, EBufferUsage::DeviceAddress)),
			"Micromap inputs and scratch must be distinct unmapped device buffers with compatible usage.");

		Result<OpacityMicromapBuildSizes> sizes = GetOpacityMicromapBuildSizes(desc.Counts, desc.Flags);

		if (!sizes)
		{
			ASSERT(sizes.GetError().Code != EErrorCode::InvalidArgument, "Micromap builds require valid usage counts and flags.");
			poisonCommandList(list);

			return Result<void>::Failure(sizes.GetError());
		}

		uint64 triangleCount = 0;

		for (const OpacityMicromapUsageCount& count : desc.Counts)
		{
			triangleCount += count.Count;
		}

		const uint64 inputAddress = backend::GetBufferDeviceAddress(m_State->m_Backend.State, input->Native);
		const uint64 triangleAddress = backend::GetBufferDeviceAddress(m_State->m_Backend.State, triangles->Native);
		const uint64 scratchAddress = backend::GetBufferDeviceAddress(m_State->m_Backend.State, scratchBuffer->Native);

		ASSERT(inputAddress != 0 && triangleAddress != 0 && scratchAddress != 0 && sizes.Value().ScratchAlignmentBytes != 0,
			"Validated micromap buffers and native sizes must provide device addresses and alignment.");
		ASSERT(!(micromap->ByteCount < sizes.Value().ResultBytes ||
				desc.InputDataOffsetBytes >= input->ByteCount ||
				desc.TriangleArrayOffsetBytes > triangles->ByteCount ||
				triangleCount * 8 > triangles->ByteCount - desc.TriangleArrayOffsetBytes ||
				inputAddress > NumericLimits<uint64>::max() - desc.InputDataOffsetBytes ||
				triangleAddress > NumericLimits<uint64>::max() - desc.TriangleArrayOffsetBytes ||
				scratchAddress > NumericLimits<uint64>::max() - scratchOffset ||
				(inputAddress + desc.InputDataOffsetBytes) % 256 != 0 ||
				(triangleAddress + desc.TriangleArrayOffsetBytes) % 256 != 0 ||
				(scratchAddress + scratchOffset) % sizes.Value().ScratchAlignmentBytes != 0 ||
				scratchOffset > scratchBuffer->ByteCount ||
				sizes.Value().BuildScratchBytes > scratchBuffer->ByteCount - scratchOffset),
			"Micromap storage, input and scratch ranges must contain the required data and satisfy alignment.");

		const auto permitsState = [&](BufferHandle handle, const BufferSlot& buffer, ETrackedResourceState required)
			{
				return (buffer.PermanentState == ETrackedResourceState::Unknown || buffer.PermanentState == required) && (command->bAutomaticBarriers || command->HasBufferState(handle, required));
			};
		ASSERT(permitsState(desc.InputData, *input, ETrackedResourceState::AccelBuildInput) &&
			permitsState(desc.TriangleArray, *triangles, ETrackedResourceState::AccelBuildInput) &&
			permitsState(scratch, *scratchBuffer, ETrackedResourceState::Storage) &&
			permitsState(micromap->Storage, storage, ETrackedResourceState::AccelStorage),
			"Micromap builds require compatible prepared buffer states.");

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + 4) ||
			!command->OpacityMicromapUses.Reserve(command->OpacityMicromapUses.Size() + 1) ||
			!command->OpacityMicromapBuildTargets.Reserve(command->OpacityMicromapBuildTargets.Size() + 1))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track micromap build resources.");
		}
		(void)command->AddBufferUse({ desc.InputData, input->Native, ETrackedResourceState::AccelBuildInput });
		(void)command->AddBufferUse({ desc.TriangleArray, triangles->Native, ETrackedResourceState::AccelBuildInput });
		(void)command->AddBufferUse({ scratch, scratchBuffer->Native, ETrackedResourceState::Storage });
		(void)command->AddBufferUse({ micromap->Storage, storage.Native, ETrackedResourceState::AccelStorage });
		++input->RecordedUses;
		++triangles->RecordedUses;
		++scratchBuffer->RecordedUses;
		++storage.RecordedUses;
		const bool bReferenceAdded = command->OpacityMicromapUses.Add(target);
		const bool bTargetAdded = command->OpacityMicromapBuildTargets.Add(target);
		ASSERT(bReferenceAdded && bTargetAdded, "Reserved micromap tracking must admit the validated build.");
		++micromap->RecordedUses;
		Result<void> recorded = backend::RecordBuildOpacityMicromap(
			m_State->m_Backend.State, command->Native, micromap->Native, desc, input->Native, desc.InputDataOffsetBytes,
			triangles->Native, desc.TriangleArrayOffsetBytes, scratchBuffer->Native, scratchOffset);
		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}
}
