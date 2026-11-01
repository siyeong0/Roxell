#pragma once

#include <MemoryResource.h>
#include <TextPrimitives.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <atomic>
#include <cstddef>

namespace rox::core
{
	struct MemoryBudgetStatistics
	{
		usize ByteLimit = 0;

		// Used bytes are successful live requests; reserved bytes are in-flight upstream requests.
		usize UsedByteCount = 0;
		usize ReservedByteCount = 0;
		usize PeakByteCount = 0;
		usize RejectedAllocationCount = 0;
	};

#if defined(_MSC_VER)
#pragma warning(push)
// Cache-line separation intentionally pads this resource; keep unrelated padding diagnostics enabled.
#pragma warning(disable : 4324)
#endif
	class BudgetMemoryResource final : public MemoryResource
	{
	public:
		BudgetMemoryResource(MemoryResource& upstream, usize byteLimit) noexcept;
		~BudgetMemoryResource() noexcept override;

		MemoryBudgetStatistics GetStatistics() const noexcept;

		DiagnosticText GetName() const noexcept override;
		EMemoryResourceThreadSafety GetThreadSafety() const noexcept override;

	protected:
		void* allocate(MemoryAllocationRequest allocation) noexcept override;
		void deallocate(void* memory, MemoryAllocationRequest allocation) noexcept override;

	private:
		void lockState() const noexcept;
		void unlockState() const noexcept;

	private:
		MemoryResource& m_Upstream;
		usize m_ByteLimit;

		// Keep mutable accounting off the upstream-reference cache line on common 64-byte-line CPUs.
		alignas(64) mutable Atomic<uint32> m_StateLock{0};
		usize m_UsedByteCount;
		usize m_ReservedByteCount;
		usize m_PeakByteCount;
		usize m_RejectedAllocationCount;
	};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace rox::core
