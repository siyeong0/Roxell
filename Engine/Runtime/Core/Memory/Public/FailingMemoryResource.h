#pragma once

#include <MemoryResource.h>
#include <TextPrimitives.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <atomic>
#include <cstddef>

namespace rox::core
{
	class FailingMemoryResource final : public MemoryResource
	{
	public:
		// The first configured attempts delegate upstream; every later attempt fails until Reset.
		FailingMemoryResource(MemoryResource& upstream, rox::core::usize allocationAttemptsBeforeFailure) noexcept;

		void Reset(rox::core::usize allocationAttemptsBeforeFailure) noexcept;

		rox::core::usize GetAllocationAttemptCount() const noexcept;
		rox::core::usize GetAllocationAttemptsBeforeFailure() const noexcept;

		rox::core::DiagnosticText GetName() const noexcept override;
		EMemoryResourceThreadSafety GetThreadSafety() const noexcept override;

	protected:
		void* allocate(MemoryAllocationRequest allocation) noexcept override;
		void deallocate(void* memory, MemoryAllocationRequest allocation) noexcept override;

	private:
		void lockState() const noexcept;
		void unlockState() const noexcept;

	private:
		MemoryResource& m_Upstream;

		mutable rox::core::Mutex m_StateLock;
		rox::core::usize m_AllocationAttemptCount;
		rox::core::usize m_AllocationAttemptsBeforeFailure;
	};
} // namespace rox::core
