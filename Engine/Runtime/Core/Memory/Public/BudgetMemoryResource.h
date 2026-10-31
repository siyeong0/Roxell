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
		rox::core::usize ByteLimit = 0;

		// Used bytes are successful live requests; reserved bytes are in-flight upstream requests.
		rox::core::usize UsedByteCount = 0;
		rox::core::usize ReservedByteCount = 0;
		rox::core::usize PeakByteCount = 0;
		rox::core::usize RejectedAllocationCount = 0;
	};

#if defined(_MSC_VER)
#pragma warning(push)
// Cache-line separation intentionally pads this resource; keep unrelated padding diagnostics enabled.
#pragma warning(disable : 4324)
#endif
	class BudgetMemoryResource final : public MemoryResource
	{
	public:
		BudgetMemoryResource(MemoryResource& upstream, rox::core::usize byteLimit) noexcept;
		~BudgetMemoryResource() noexcept override;

		MemoryBudgetStatistics GetStatistics() const noexcept;

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
		rox::core::usize m_ByteLimit;

		// Keep mutable accounting off the upstream-reference cache line on common 64-byte-line CPUs.
		alignas(64) mutable Atomic<uint32> m_StateLock{0};
		rox::core::usize m_UsedByteCount;
		rox::core::usize m_ReservedByteCount;
		rox::core::usize m_PeakByteCount;
		rox::core::usize m_RejectedAllocationCount;
	};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace rox::core
