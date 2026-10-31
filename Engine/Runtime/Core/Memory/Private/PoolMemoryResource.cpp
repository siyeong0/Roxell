#include <PoolMemoryResource.h>
#include "MemoryTraceScope.hpp"
#include <TextPrimitives.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Utility.hpp>
#include <Assertion.h>

namespace rox::core
{
	namespace
	{
		constexpr usize BITMAP_WORD_BIT_COUNT = sizeof(usize) * 8;

		constexpr usize getBitmapWordCount(usize count) noexcept
		{
			return count == 0 ? 0 : 1 + (count - 1) / BITMAP_WORD_BIT_COUNT;
		}

		template <typename T>
		usize replaceSlotValue(Byte* slot, usize value) noexcept
		{
			T previous;
			const T narrowed = static_cast<T>(value);
			CopyBytes(&previous, slot, sizeof(previous));
			CopyBytes(slot, &narrowed, sizeof(narrowed));
			return previous;
		}

		usize findNextSetBitRecursive(const usize* bitmapWords, usize wordCount, usize firstBitIndex) noexcept
		{
			const usize wordIndex = firstBitIndex / BITMAP_WORD_BIT_COUNT;
			if (wordIndex >= wordCount)
			{
				return wordCount * BITMAP_WORD_BIT_COUNT;
			}

			const usize remainingBits =
			    bitmapWords[wordIndex] & (NumericLimits<usize>::max() << (firstBitIndex % BITMAP_WORD_BIT_COUNT));
			if (remainingBits != 0)
			{
				return wordIndex * BITMAP_WORD_BIT_COUNT + static_cast<usize>(CountTrailingZeros(remainingBits));
			}

			if (wordCount == 1)
			{
				return BITMAP_WORD_BIT_COUNT;
			}

			const usize nextWordIndex = findNextSetBitRecursive(bitmapWords + wordCount, getBitmapWordCount(wordCount), wordIndex + 1);
			return nextWordIndex >= wordCount
			           ? wordCount * BITMAP_WORD_BIT_COUNT
			           : nextWordIndex * BITMAP_WORD_BIT_COUNT + static_cast<usize>(CountTrailingZeros(bitmapWords[nextWordIndex]));
		}
	}

	PoolMemoryResource::Layout PoolMemoryResource::makeLayout(usize blockByteCount, usize blockAlignment,
	                                                        usize blockCapacity) noexcept
	{
		ASSERT(blockByteCount != 0, "A memory pool requires a positive block byte count.");
		ASSERT(IsValidMemoryAlignment(blockAlignment), "Memory pool alignment must be a non-zero power of two.");

		constexpr usize MAX_STORAGE_BYTE_COUNT = static_cast<usize>(NumericLimits<isize>::max());
		ASSERT(blockByteCount <= MAX_STORAGE_BYTE_COUNT && blockAlignment - 1 <= MAX_STORAGE_BYTE_COUNT - blockByteCount, "Memory pool aligned stride must support defined pointer arithmetic.");
		const usize blockStride = (blockByteCount + blockAlignment - 1) & ~(blockAlignment - 1);
		ASSERT(blockCapacity <= MAX_STORAGE_BYTE_COUNT / blockStride, "Memory pool capacity must support defined pointer arithmetic.");

		const usize alignmentExponent = static_cast<usize>(CountTrailingZeros(blockAlignment));
		const usize alignmentBitCount = BitWidth(alignmentExponent);
		usize slotByteCount = sizeof(MemoryAllocationRequest);
		if (blockByteCount - 1 <= (NumericLimits<usize>::max() >> alignmentBitCount))
		{
			const usize maximumValue = Max(blockCapacity, ((blockByteCount - 1) << alignmentBitCount) | alignmentExponent);
			slotByteCount = 1;
			while (slotByteCount < sizeof(usize) && maximumValue >> (slotByteCount * 8) != 0)
			{
				slotByteCount *= 2;
			}
		}

		// Leaves describe live slots; each higher level identifies nonempty words below it.
		usize bitmapWordCount = 0;
		for (usize words = getBitmapWordCount(blockCapacity); words != 0; words = words == 1 ? 0 : getBitmapWordCount(words))
		{
			bitmapWordCount += words;
		}

		const usize bitmapByteCount = bitmapWordCount * sizeof(usize);
		ASSERT(blockCapacity <= (MAX_STORAGE_BYTE_COUNT - bitmapByteCount) / slotByteCount, "Memory pool metadata must fit.");
		const usize metadataByteCount = bitmapByteCount + blockCapacity * slotByteCount;

		ASSERT(blockAlignment - 1 <= MAX_STORAGE_BYTE_COUNT - metadataByteCount, "Memory pool metadata padding must fit.");
		const usize payloadByteOffset = (metadataByteCount + blockAlignment - 1) & ~(blockAlignment - 1);
		const usize payloadByteCount = blockCapacity * blockStride;
		ASSERT(payloadByteCount <= MAX_STORAGE_BYTE_COUNT - payloadByteOffset, "Memory pool storage byte count must fit.");

		return {blockByteCount, blockAlignment, blockCapacity, blockStride,
		        payloadByteOffset, bitmapByteCount, slotByteCount, alignmentBitCount,
		        {payloadByteOffset + payloadByteCount, Max(blockAlignment, alignof(usize))}};
	}

	MemoryAllocationRequest PoolMemoryResource::GetStorageRequest(usize blockByteCount, usize blockAlignment, usize blockCapacity) noexcept
	{
		return makeLayout(blockByteCount, blockAlignment, blockCapacity).StorageRequest;
	}

	PoolMemoryResource::PoolMemoryResource(MemoryResource& upstream) noexcept
	    : m_Upstream(upstream)
	{
	}

	bool PoolMemoryResource::Initialize(usize blockByteCount, usize blockAlignment, usize blockCapacity) noexcept
	{
		ASSERT(!m_bInitialized, "A memory pool can only be initialized once.");

		const Layout layout = makeLayout(blockByteCount, blockAlignment, blockCapacity);
		if (!MemoryBlock::CreateInto(m_Upstream, &m_Storage, layout.StorageRequest))
		{
			return false;
		}

		m_Layout = layout;
		m_LiveBitmap = static_cast<usize*>(m_Storage.GetData());
		m_SlotRecords = blockCapacity == 0 ? nullptr : static_cast<Byte*>(m_Storage.GetData()) + layout.BitmapByteCount;
		for (usize word = 0; word < layout.BitmapByteCount / sizeof(usize); ++word)
		{
			ConstructAt(m_LiveBitmap + word, usize{0});
		}

		m_Payload = blockCapacity == 0 ? nullptr : static_cast<Byte*>(m_Storage.GetData()) + layout.PayloadByteOffset;
		for (usize index = 0; index < blockCapacity; ++index)
		{
			writeSlotValue(index, index + 1);
		}

		m_bInitialized = true;
		return true;
	}

	bool PoolMemoryResource::IsInitialized() const noexcept
	{
		return m_bInitialized;
	}

	PoolMemoryResource::~PoolMemoryResource()
	{
		ASSERT(m_LiveBlockCount == 0, "A memory pool must not be destroyed with live allocations.");
	}

	usize PoolMemoryResource::GetBlockCapacity() const noexcept
	{
		return m_Layout.BlockCapacity;
	}

	usize PoolMemoryResource::GetLiveBlockCount() const noexcept
	{
		return m_LiveBlockCount;
	}

	usize PoolMemoryResource::GetBlockByteCount() const noexcept
	{
		return m_Layout.BlockByteCount;
	}

	usize PoolMemoryResource::GetBlockAlignment() const noexcept
	{
		return m_Layout.BlockAlignment;
	}

	MemoryResource& PoolMemoryResource::GetUpstreamResource() const noexcept
	{
		return m_Upstream;
	}

	DiagnosticText PoolMemoryResource::GetName() const noexcept
	{
		return "PoolMemoryResource";
	}

	EMemoryResourceThreadSafety PoolMemoryResource::GetThreadSafety() const noexcept
	{
		return EMemoryResourceThreadSafety::CallerConfined;
	}

	void* PoolMemoryResource::allocate(const MemoryAllocationRequest allocation) noexcept
	{
		ASSERT(m_bInitialized, "A memory pool must be initialized before allocation.");
		if (allocation.ByteCount > m_Layout.BlockByteCount || allocation.Alignment > m_Layout.BlockAlignment || m_FreeSlotIndex == m_Layout.BlockCapacity)
		{
			return nullptr;
		}

		ASSERT(m_FreeSlotIndex < m_Layout.BlockCapacity, "A memory pool free-list head must identify an available slot.");
		const usize index = m_FreeSlotIndex;
		usize& word = m_LiveBitmap[index / BITMAP_WORD_BIT_COUNT];
		const usize bit = usize{1} << (index % BITMAP_WORD_BIT_COUNT);
		ASSERT((word & bit) == 0, "A memory pool free-list slot must not contain a live allocation.");
		m_FreeSlotIndex = occupySlot(index, allocation);
		const bool bWasEmpty = word == 0;
		word |= bit;
		if (bWasEmpty)
		{
			updateLiveSummary(index, true);
		}

		++m_LiveBlockCount;
		return m_Payload + index * m_Layout.BlockStride;
	}

	usize PoolMemoryResource::liveSlotIndex(const void* const memory) const noexcept
	{
		const uintptr address = reinterpret_cast<uintptr>(memory);
		const uintptr base = reinterpret_cast<uintptr>(m_Payload);
		ASSERT(address >= base && address - base < m_Layout.BlockCapacity * m_Layout.BlockStride, "A memory pool release must belong to this pool's storage.");
		const usize offset = static_cast<usize>(address - base);
		ASSERT(offset % m_Layout.BlockStride == 0, "A memory pool release must point to the start of a slot.");
		const usize index = offset / m_Layout.BlockStride;
		ASSERT((m_LiveBitmap[index / BITMAP_WORD_BIT_COUNT] & (usize{1} << (index % BITMAP_WORD_BIT_COUNT))) != 0, "A memory pool release must identify a live allocation.");
		return index;
	}

	void PoolMemoryResource::deallocate(void* const memory, const MemoryAllocationRequest allocation) noexcept
	{
		const usize index = liveSlotIndex(memory);
		const MemoryAllocationRequest original = readSlotAllocation(index);
		ASSERT(original.ByteCount == allocation.ByteCount && original.Alignment == allocation.Alignment,
		    "A memory pool release must match the original byte count and alignment.");
		releaseSlot(index);
	}

	void PoolMemoryResource::releaseSlot(usize index) noexcept
	{
		usize& word = m_LiveBitmap[index / BITMAP_WORD_BIT_COUNT];
		word &= ~(usize{1} << (index % BITMAP_WORD_BIT_COUNT));
		if (word == 0)
		{
			updateLiveSummary(index, false);
		}

		writeSlotValue(index, m_FreeSlotIndex);
		m_FreeSlotIndex = index;
		--m_LiveBlockCount;
	}

	void PoolMemoryResource::releaseObjectSlot(usize index) noexcept
	{
		// ObjectPool has already checked ownership; its request is fixed by T. Keep each logical trace event.
		detail::MemoryTraceScope trace(static_cast<MemoryResource*>(this));
		trace.Deallocating(m_Payload + index * m_Layout.BlockStride, m_Layout.BlockByteCount);
		releaseSlot(index);
	}

	void* PoolMemoryResource::nextLiveSlotStorage(usize* const inOutIndex) noexcept
	{
		ASSERT(m_LiveBlockCount != 0, "A memory pool live-slot lookup requires a live allocation.");
		const usize index = findNextSetBitRecursive(m_LiveBitmap, getBitmapWordCount(m_Layout.BlockCapacity), *inOutIndex);
		ASSERT(index < m_Layout.BlockCapacity, "A memory pool live-slot walk must identify a remaining live allocation.");
		*inOutIndex = index;
		return m_Payload + index * m_Layout.BlockStride;
	}

	// The leaf word has already changed between empty and nonempty; only ancestors need updating.
	void PoolMemoryResource::updateLiveSummary(usize index, bool bLive) noexcept
	{
		usize levelWordOffset = 0;
		usize levelWordCount = getBitmapWordCount(m_Layout.BlockCapacity);
		while (levelWordCount > 1)
		{
			index /= BITMAP_WORD_BIT_COUNT;
			levelWordOffset += levelWordCount;
			levelWordCount = getBitmapWordCount(levelWordCount);
			usize& summaryWord = m_LiveBitmap[levelWordOffset + index / BITMAP_WORD_BIT_COUNT];
			const usize previousWord = summaryWord;
			const usize bit = usize{1} << (index % BITMAP_WORD_BIT_COUNT);
			summaryWord = bLive ? summaryWord | bit : summaryWord & ~bit;
			if ((previousWord != 0) == (summaryWord != 0))
			{
				return;
			}
		}
	}

	usize PoolMemoryResource::readSlotValue(usize index) const noexcept
	{
		const Byte* const slot = m_SlotRecords + index * m_Layout.SlotByteCount;
		switch (m_Layout.SlotByteCount)
		{
		case 1:
		{
			uint8 value;
			CopyBytes(&value, slot, sizeof(value));
			return value;
		}
		case 2:
		{
			uint16 value;
			CopyBytes(&value, slot, sizeof(value));
			return value;
		}
		case 4:
		{
			uint32 value;
			CopyBytes(&value, slot, sizeof(value));
			return value;
		}
		default:
		{
			usize value;
			CopyBytes(&value, slot, sizeof(value));
			return value;
		}
		}
	}

	void PoolMemoryResource::writeSlotValue(usize index, usize value) noexcept
	{
		Byte* const slot = m_SlotRecords + index * m_Layout.SlotByteCount;
		switch (m_Layout.SlotByteCount)
		{
		case 1:
		{
			const uint8 narrow = static_cast<uint8>(value);
			CopyBytes(slot, &narrow, sizeof(narrow));
			break;
		}
		case 2:
		{
			const uint16 narrow = static_cast<uint16>(value);
			CopyBytes(slot, &narrow, sizeof(narrow));
			break;
		}
		case 4:
		{
			const uint32 narrow = static_cast<uint32>(value);
			CopyBytes(slot, &narrow, sizeof(narrow));
			break;
		}
		default:
		{
			CopyBytes(slot, &value, sizeof(value));
			break;
		}
		}
	}

	MemoryAllocationRequest PoolMemoryResource::readSlotAllocation(usize index) const noexcept
	{
		if (m_Layout.SlotByteCount > sizeof(usize))
		{
			MemoryAllocationRequest allocation;
			CopyBytes(&allocation, m_SlotRecords + index * m_Layout.SlotByteCount, sizeof(allocation));
			return allocation;
		}

		// Low bits hold log2(alignment); the remaining bits hold byteCount - 1.
		const usize value = readSlotValue(index);
		const usize exponentMask = (usize{1} << m_Layout.AlignmentBitCount) - 1;
		return {(value >> m_Layout.AlignmentBitCount) + 1, usize{1} << (value & exponentMask)};
	}

	usize PoolMemoryResource::occupySlot(usize index, MemoryAllocationRequest allocation) noexcept
	{
		// Replace the free-list link with the request using one record-width dispatch.
		Byte* const slot = m_SlotRecords + index * m_Layout.SlotByteCount;
		if (m_Layout.SlotByteCount > sizeof(usize))
		{
			usize next;
			CopyBytes(&next, slot, sizeof(next));
			CopyBytes(slot, &allocation, sizeof(allocation));
			return next;
		}

		const usize value = ((allocation.ByteCount - 1) << m_Layout.AlignmentBitCount) |
		                    static_cast<usize>(CountTrailingZeros(allocation.Alignment));
		switch (m_Layout.SlotByteCount)
		{
		case 1: return replaceSlotValue<uint8>(slot, value);
		case 2: return replaceSlotValue<uint16>(slot, value);
		case 4: return replaceSlotValue<uint32>(slot, value);
		default: return replaceSlotValue<usize>(slot, value);
		}
	}
} // namespace rox::core
