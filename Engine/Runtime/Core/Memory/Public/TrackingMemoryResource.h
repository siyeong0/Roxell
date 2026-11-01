#pragma once

#include <MemoryResource.h>
#include <TextPrimitives.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <atomic>
#include <cstddef>

namespace rox::core
{
	struct MemoryStatistics
	{
		usize LiveAllocationCount = 0;
		usize LiveByteCount = 0;
		usize PeakByteCount = 0;
	};

#if defined(_MSC_VER)
#pragma warning(push)
// Cache-line separation intentionally pads this resource; keep unrelated padding diagnostics enabled.
#pragma warning(disable : 4324)
#endif
	class TrackingMemoryResource final : public MemoryResource
	{
	public:
		explicit TrackingMemoryResource(MemoryResource& upstream) noexcept;
		~TrackingMemoryResource() noexcept override;

		MemoryStatistics GetStatistics() const noexcept;

		DiagnosticText GetName() const noexcept override;
		EMemoryResourceThreadSafety GetThreadSafety() const noexcept override;

	protected:
		void* allocate(MemoryAllocationRequest allocation) noexcept override;
		void deallocate(void* memory, MemoryAllocationRequest allocation) noexcept override;

	private:
		void lockStatistics() const noexcept;
		void unlockStatistics() const noexcept;

	private:
		MemoryResource& m_Upstream;

		// Keep mutable accounting off the upstream-reference cache line on common 64-byte-line CPUs.
		alignas(64) mutable Atomic<uint32> m_StatisticsLock{0};
		usize m_LiveAllocationCount;
		usize m_LiveByteCount;
		usize m_PeakByteCount;
	};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace rox::core
