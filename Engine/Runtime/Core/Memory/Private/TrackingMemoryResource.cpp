#include <TrackingMemoryResource.h>
#include "MemoryAccountingLock.hpp"
#include <TextPrimitives.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Assertion.h>

#include <new>

namespace rox::core
{
	namespace
	{
		struct TrackingHeader
		{
			TrackingMemoryResource* Owner;
			MemoryAllocationRequest RequestedAllocation;
		};
	}

	TrackingMemoryResource::TrackingMemoryResource(MemoryResource& upstream) noexcept
	    : m_Upstream(upstream)
	    , m_LiveAllocationCount(0)
	    , m_LiveByteCount(0)
	    , m_PeakByteCount(0)
	{
	}

	TrackingMemoryResource::~TrackingMemoryResource() noexcept
	{
		lockStatistics();
		const bool bHasLiveAllocations = m_LiveAllocationCount != 0 || m_LiveByteCount != 0;
		unlockStatistics();
		ASSERT(!bHasLiveAllocations, "A tracking memory resource must outlive every tracked allocation.");
	}

	MemoryStatistics TrackingMemoryResource::GetStatistics() const noexcept
	{
		lockStatistics();
		const MemoryStatistics statistics{m_LiveAllocationCount, m_LiveByteCount, m_PeakByteCount};
		unlockStatistics();
		return statistics;
	}

	DiagnosticText TrackingMemoryResource::GetName() const noexcept
	{
		return "TrackingMemoryResource";
	}

	EMemoryResourceThreadSafety TrackingMemoryResource::GetThreadSafety() const noexcept
	{
		return m_Upstream.GetThreadSafety();
	}

	void* TrackingMemoryResource::allocate(const MemoryAllocationRequest allocation) noexcept
	{
		const usize effectiveAlignment = rox::core::Max(allocation.Alignment, alignof(TrackingHeader));
		const usize alignmentMask = effectiveAlignment - 1;
		usize paddedHeaderSize = 0;

		if (!AddMemorySize(sizeof(TrackingHeader), alignmentMask, &paddedHeaderSize))
		{
			return nullptr;
		}

		const usize userOffset = paddedHeaderSize & ~alignmentMask;
		usize upstreamByteCount = 0;

		if (!AddMemorySize(userOffset, allocation.ByteCount, &upstreamByteCount))
		{
			return nullptr;
		}

		if (upstreamByteCount > static_cast<usize>(rox::core::NumericLimits<isize>::max()))
		{
			return nullptr;
		}

		const MemoryAllocationRequest upstreamAllocation{upstreamByteCount, effectiveAlignment};
		void* const upstreamMemory = m_Upstream.Allocate(upstreamAllocation);

		if (upstreamMemory == nullptr)
		{
			return nullptr;
		}

		Byte* const userMemory = static_cast<Byte*>(upstreamMemory) + userOffset;
		TrackingHeader* const header = reinterpret_cast<TrackingHeader*>(userMemory - sizeof(TrackingHeader));
		::new (header) TrackingHeader{this, allocation};

		lockStatistics();
		ASSERT(m_LiveAllocationCount != NumericLimits<usize>::max() && allocation.ByteCount <= NumericLimits<usize>::max() - m_LiveByteCount, "Live memory statistics must fit the address space.");
		++m_LiveAllocationCount;
		m_LiveByteCount += allocation.ByteCount;
		m_PeakByteCount = Max(m_PeakByteCount, m_LiveByteCount);
		unlockStatistics();
		return userMemory;
	}

	void TrackingMemoryResource::deallocate(void* const memory, const MemoryAllocationRequest allocation) noexcept
	{
		TrackingHeader* const header = reinterpret_cast<TrackingHeader*>(reinterpret_cast<Byte*>(memory) - sizeof(TrackingHeader));
		ASSERT(header->Owner == this, "Storage must be deallocated by the TrackingMemoryResource that allocated it.");
		ASSERT(header->RequestedAllocation.ByteCount == allocation.ByteCount && header->RequestedAllocation.Alignment == allocation.Alignment, "Deallocation metadata must match the original tracked allocation request.");

		lockStatistics();
		ASSERT(m_LiveAllocationCount != 0, "A tracked deallocation requires a matching live allocation.");
		ASSERT(m_LiveByteCount >= header->RequestedAllocation.ByteCount, "A tracked deallocation must not underflow the live byte count.");
		--m_LiveAllocationCount;
		m_LiveByteCount -= header->RequestedAllocation.ByteCount;
		unlockStatistics();

		const usize alignment = Max(allocation.Alignment, alignof(TrackingHeader));
		const usize userOffset = (sizeof(TrackingHeader) + alignment - 1) & ~(alignment - 1);
		void* const upstreamMemory = static_cast<Byte*>(memory) - userOffset;
		const MemoryAllocationRequest upstreamAllocation{userOffset + allocation.ByteCount, alignment};
		header->~TrackingHeader();
		m_Upstream.Deallocate(upstreamMemory, upstreamAllocation);
	}

	void TrackingMemoryResource::lockStatistics() const noexcept
	{
		detail::LockMemoryAccounting(m_StatisticsLock);
	}

	void TrackingMemoryResource::unlockStatistics() const noexcept
	{
		detail::UnlockMemoryAccounting(m_StatisticsLock);
	}
} // namespace rox::core
