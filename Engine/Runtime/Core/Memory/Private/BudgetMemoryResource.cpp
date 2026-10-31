#include <BudgetMemoryResource.h>
#include "MemoryAccountingLock.hpp"
#include <TextPrimitives.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Assertion.h>

namespace rox::core
{
	BudgetMemoryResource::BudgetMemoryResource(MemoryResource& upstream, const usize byteLimit) noexcept
	    : m_Upstream(upstream)
	    , m_ByteLimit(byteLimit)
	    , m_UsedByteCount(0)
	    , m_ReservedByteCount(0)
	    , m_PeakByteCount(0)
	    , m_RejectedAllocationCount(0)
	{
	}

	BudgetMemoryResource::~BudgetMemoryResource() noexcept
	{
		lockState();
		const bool bHasOutstandingBytes = m_UsedByteCount != 0 || m_ReservedByteCount != 0;
		unlockState();
		ASSERT(!bHasOutstandingBytes, "A budget memory resource must outlive every allocation and in-flight request.");
	}

	MemoryBudgetStatistics BudgetMemoryResource::GetStatistics() const noexcept
	{
		lockState();
		const MemoryBudgetStatistics statistics{m_ByteLimit, m_UsedByteCount, m_ReservedByteCount, m_PeakByteCount, m_RejectedAllocationCount};
		unlockState();
		return statistics;
	}

	DiagnosticText BudgetMemoryResource::GetName() const noexcept
	{
		return "BudgetMemoryResource";
	}

	EMemoryResourceThreadSafety BudgetMemoryResource::GetThreadSafety() const noexcept
	{
		return m_Upstream.GetThreadSafety();
	}

	void* BudgetMemoryResource::allocate(const MemoryAllocationRequest allocation) noexcept
	{
		lockState();
		ASSERT(m_UsedByteCount <= m_ByteLimit && m_ReservedByteCount <= m_ByteLimit - m_UsedByteCount, "Budget state must remain within its byte limit.");
		const bool bLimitExceeded = allocation.ByteCount > m_ByteLimit - m_UsedByteCount - m_ReservedByteCount;

		if (bLimitExceeded)
		{
			if (m_RejectedAllocationCount != rox::core::NumericLimits<usize>::max())
			{
				++m_RejectedAllocationCount;
			}

			unlockState();
			return nullptr;
		}

		m_ReservedByteCount += allocation.ByteCount;
		unlockState();

		void* const memory = m_Upstream.Allocate(allocation);

		lockState();
		ASSERT(m_ReservedByteCount >= allocation.ByteCount,
		       "A completed budget allocation must have a matching reserved byte count.");
		m_ReservedByteCount -= allocation.ByteCount;

		if (memory != nullptr)
		{
			m_UsedByteCount += allocation.ByteCount;
			m_PeakByteCount = rox::core::Max(m_PeakByteCount, m_UsedByteCount);
		}

		unlockState();
		return memory;
	}

	void BudgetMemoryResource::deallocate(void* const memory, const MemoryAllocationRequest allocation) noexcept
	{
		lockState();
		ASSERT(m_UsedByteCount >= allocation.ByteCount, "A budget deallocation must not underflow the tracked live byte count.");
		m_UsedByteCount -= allocation.ByteCount;
		unlockState();

		m_Upstream.Deallocate(memory, allocation);
	}

	void BudgetMemoryResource::lockState() const noexcept
	{
		detail::LockMemoryAccounting(m_StateLock);
	}

	void BudgetMemoryResource::unlockState() const noexcept
	{
		detail::UnlockMemoryAccounting(m_StateLock);
	}
} // namespace rox::core
