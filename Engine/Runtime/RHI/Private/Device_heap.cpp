#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<uint32> Device::QueryMemoryBudget(Span<MemoryBudget> budgets) const
	{
		CallScope call(*this, true);

		return backend::QueryMemoryBudget(m_State->m_Backend.State, budgets);
	}

	[[nodiscard]] HeapSlot* Device::getHeap(HeapHandle handle) noexcept
	{
		ASSERT(handle.IsValid(), "Heap access requires a valid handle.");
		ASSERT(handle.Owner == m_State->m_Owner, "The heap handle must belong to this device.");
		ASSERT(handle.Slot < m_State->m_Heaps.Size(), "The heap handle must identify an existing slot.");

		HeapSlot& slot = m_State->m_Heaps[handle.Slot];
		ASSERT(slot.bLive, "The heap handle must identify a live heap.");
		ASSERT(slot.Generation == handle.Generation, "The heap handle must match the current slot generation.");

		return &slot;
	}

	[[nodiscard]] Result<void> Device::collectRetiredRanges()
	{
		backend::Collect(m_State->m_Backend.State);

		for (usize index = 0; index < m_State->m_RetiredRanges.Size();)
		{
			Result<bool> complete = backend::IsComplete(m_State->m_Backend.State, m_State->m_RetiredRanges[index].Serial);

			if (!complete)
			{
				return Result<void>::Failure(complete.GetError());
			}

			if (complete.Value())
			{
				backend::Collect(m_State->m_Backend.State);
				m_State->m_RetiredRanges.RemoveAtSwap(index);
			}
			else
			{
				++index;
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<HeapHandle> Device::CreateHeap(const HeapDesc& desc)
	{
		CallScope call(*this, false);
		constexpr uint64 HEAP_ALIGNMENT = 64ull * 1024ull;
		const uint64 alignment = desc.Usage == EHeapUsage::Attachments ? 4ull * 1024ull * 1024ull : HEAP_ALIGNMENT;

		ASSERT(!(desc.CapacityBytes == 0 ||
				desc.CapacityBytes % alignment != 0 ||
				desc.Memory > EBufferMemory::CpuRead ||
				desc.Usage > EHeapUsage::Attachments ||
				(desc.Usage != EHeapUsage::Buffers && desc.Memory != EBufferMemory::Device)),
			"Heap capacity, resource class, or memory type is invalid.");

		Result<void*> created = backend::CreateHeap(m_State->m_Backend.State, desc);

		if (!created)
		{
			return Result<HeapHandle>::Failure(created.GetError());
		}

		const bool bNewSlot = m_State->m_FreeHeap == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Heaps.Size()) : m_State->m_FreeHeap;

		if (bNewSlot && (m_State->m_Heaps.Size() >= NumericLimits<uint32>::max() || !m_State->m_Heaps.Add(HeapSlot{})))
		{
			backend::DestroyHeapNow(m_State->m_Backend.State, created.Value());

			return Result<HeapHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a heap handle slot.");
		}
		HeapSlot& slot = m_State->m_Heaps[index];

		if (!bNewSlot)
		{
			m_State->m_FreeHeap = slot.NextFree;
		}
		slot.Native = created.Value();
		slot.Descriptor = desc;
		slot.LiveBuffers = 0;
		slot.LiveTextures = 0;
		slot.SparseTileRefs = 0;
		slot.PreparedSparseReferences = 0;
		slot.bUntrackedSparseUses = false;
		slot.NextFree = NumericLimits<uint32>::max();
		slot.bLive = true;

		return Result<HeapHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyHeap(HeapHandle handle)
	{
		CallScope call(*this, false);

		return destroyHeapImpl(handle, true);
	}

	[[nodiscard]] Result<void> Device::TryDestroyHeap(HeapHandle handle)
	{
		CallScope call(*this, false);

		return destroyHeapImpl(handle, false);
	}

	[[nodiscard]] Result<void> Device::destroyHeapImpl(HeapHandle handle, bool bWait)
	{
		collectPreparedTextureMappings(false);
		HeapSlot* const slot = getHeap(handle);

		if (slot->LiveBuffers != 0 || slot->LiveTextures != 0 || slot->SparseTileRefs != 0 || slot->PreparedSparseReferences != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.Heap", "Live resources still reference the heap.");
		}

		if (bWait)
		{
			if (hasAnyNativeQueueAccess())
			{
				return Result<void>::Failure(nativeQueueAccessConflict());
			}

			Result<void> idle = backend::WaitIdle(m_State->m_Backend.State);

			if (!idle)
			{
				return idle;
			}
		}
		else
		{
			if (slot->Descriptor.Usage != EHeapUsage::Buffers && slot->bUntrackedSparseUses)
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.Heap", "Nonblocking retirement requires a buffer heap.");
			}

			Result<void> collected = collectRetiredRanges();

			if (!collected)
			{
				return collected;
			}

			for (rhi::RetiredHeapRange const& range : m_State->m_RetiredRanges)
			{
				if (range.Heap == handle)
				{
					return Result<void>::Failure(EErrorCode::Conflict, "Rhi.Heap", "Submitted GPU work still uses this buffer heap.");
				}
			}
		}

		Result<void> destroyed = backend::DestroyHeap(m_State->m_Backend.State, slot->Native);

		if (!destroyed)
		{
			return destroyed;
		}

		for (usize index = 0; index < m_State->m_RetiredRanges.Size();)
		{
			if (m_State->m_RetiredRanges[index].Heap == handle)
			{
				m_State->m_RetiredRanges.RemoveAtSwap(index);
			}
			else
			{
				++index;
			}
		}
		slot->Native = nullptr;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeHeap;
			m_State->m_FreeHeap = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint64> Device::GetHeapCommittedBytes(HeapHandle handle) const
	{
		CallScope call(*this, false);
		ASSERT(handle.IsValid() && handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Heaps.Size(), "RHI operations require valid handles for this device.");
		rhi::HeapSlot const& slot = m_State->m_Heaps[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "RHI operations require valid handles for this device.");
		if constexpr (backend::HAS_GET_HEAP_COMMITTED_BYTES == false)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.Heap", "The backend does not expose native heap allocation bytes.");
		}

		return Result<uint64>::Success(backend::GetHeapCommittedBytes(slot.Native));
	}
}
