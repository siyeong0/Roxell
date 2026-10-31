#pragma once

#include <MemoryBlock.h>

namespace rox::core
{
	template <typename T> class ObjectPool;

	// Caller-confined fixed-size, fixed-capacity storage. Release every block before destruction.
	class PoolMemoryResource final : public MemoryResource
	{
		template <typename T> friend class ObjectPool;
		struct Layout
		{
			usize BlockByteCount = 0;
			usize BlockAlignment = 1;
			usize BlockCapacity = 0;
			usize BlockStride = 0;

			usize PayloadByteOffset = 0;
			usize BitmapByteCount = 0;

			// A slot holds either a free index or the exact live request, encoded at the smallest usable width.
			usize SlotByteCount = 0;
			usize AlignmentBitCount = 0;

			MemoryAllocationRequest StorageRequest;
		};

	public:
		explicit PoolMemoryResource(MemoryResource& upstream) noexcept;
		~PoolMemoryResource() override;

		// One upstream allocation covers payload, metadata and padding. Invalid layouts assert.
		[[nodiscard]] static MemoryAllocationRequest GetStorageRequest(usize blockByteCount, usize blockAlignment,
		                                                              usize blockCapacity) noexcept;
		// Failure leaves the resource uninitialized and retryable. Zero capacity succeeds.
		[[nodiscard]] bool Initialize(usize blockByteCount, usize blockAlignment, usize blockCapacity) noexcept;
		[[nodiscard]] bool IsInitialized() const noexcept;
		usize GetBlockCapacity() const noexcept;
		usize GetLiveBlockCount() const noexcept;
		usize GetBlockByteCount() const noexcept;
		usize GetBlockAlignment() const noexcept;
		MemoryResource& GetUpstreamResource() const noexcept;

		DiagnosticText GetName() const noexcept override;
		EMemoryResourceThreadSafety GetThreadSafety() const noexcept override;

	protected:
		void* allocate(MemoryAllocationRequest allocation) noexcept override;
		void deallocate(void* memory, MemoryAllocationRequest allocation) noexcept override;

	private:
		static Layout makeLayout(usize blockByteCount, usize blockAlignment, usize blockCapacity) noexcept;
		usize liveSlotIndex(const void* memory) const noexcept;
		void* nextLiveSlotStorage(usize* inOutIndex) noexcept;
		void updateLiveSummary(usize index, bool bLive) noexcept;
		void releaseSlot(usize index) noexcept;
		void releaseObjectSlot(usize index) noexcept;
		usize readSlotValue(usize index) const noexcept;
		void writeSlotValue(usize index, usize value) noexcept;
		MemoryAllocationRequest readSlotAllocation(usize index) const noexcept;
		usize occupySlot(usize index, MemoryAllocationRequest allocation) noexcept;

	private:
		MemoryResource& m_Upstream;
		Layout m_Layout;

		MemoryBlock m_Storage;

		Byte* m_SlotRecords = nullptr;
		usize* m_LiveBitmap = nullptr;
		Byte* m_Payload = nullptr;

		usize m_FreeSlotIndex = 0;
		usize m_LiveBlockCount = 0;
		bool m_bInitialized = false;
	};
} // namespace rox::core
