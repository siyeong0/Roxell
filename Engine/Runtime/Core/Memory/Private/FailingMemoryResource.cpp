#include <FailingMemoryResource.h>
#include <TextPrimitives.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <NumericLimits.hpp>

namespace rox::core
{
	FailingMemoryResource::FailingMemoryResource(MemoryResource& upstream,
	                                             const usize allocationAttemptsBeforeFailure) noexcept
	    : m_Upstream(upstream)
	    , m_AllocationAttemptCount(0)
	    , m_AllocationAttemptsBeforeFailure(allocationAttemptsBeforeFailure)
	{
	}

	void FailingMemoryResource::Reset(const usize allocationAttemptsBeforeFailure) noexcept
	{
		lockState();
		m_AllocationAttemptsBeforeFailure = allocationAttemptsBeforeFailure;
		m_AllocationAttemptCount = 0;
		unlockState();
	}

	usize FailingMemoryResource::GetAllocationAttemptCount() const noexcept
	{
		lockState();
		const usize allocationAttemptCount = m_AllocationAttemptCount;
		unlockState();
		return allocationAttemptCount;
	}

	usize FailingMemoryResource::GetAllocationAttemptsBeforeFailure() const noexcept
	{
		lockState();
		const usize allocationAttemptsBeforeFailure = m_AllocationAttemptsBeforeFailure;
		unlockState();
		return allocationAttemptsBeforeFailure;
	}

	DiagnosticText FailingMemoryResource::GetName() const noexcept
	{
		return "FailingMemoryResource";
	}

	EMemoryResourceThreadSafety FailingMemoryResource::GetThreadSafety() const noexcept
	{
		return m_Upstream.GetThreadSafety();
	}

	void* FailingMemoryResource::allocate(const MemoryAllocationRequest allocation) noexcept
	{
		lockState();
		const bool bShouldDelegate = m_AllocationAttemptCount < m_AllocationAttemptsBeforeFailure;

		if (m_AllocationAttemptCount != rox::core::NumericLimits<usize>::max())
		{
			++m_AllocationAttemptCount;
		}

		unlockState();

		if (!bShouldDelegate)
		{
			return nullptr;
		}

		return m_Upstream.Allocate(allocation);
	}

	void FailingMemoryResource::deallocate(void* const memory, const MemoryAllocationRequest allocation) noexcept
	{
		m_Upstream.Deallocate(memory, allocation);
	}

	void FailingMemoryResource::lockState() const noexcept
	{
		m_StateLock.lock();
	}

	void FailingMemoryResource::unlockState() const noexcept
	{
		m_StateLock.unlock();
	}
} // namespace rox::core
