#include <MemoryResource.h>
#include <Synchronization.hpp>
#include <Types.h>
#include "MemoryTraceScope.hpp"

#include <Assertion.h>
#include <Log.h>

#include <cstdlib>

namespace rox::core
{
	namespace
	{
		rox::core::Atomic<MemoryFailureHandler> s_FailureHandler = nullptr;
		thread_local bool s_bReportingFailure = false;
	}

	MemoryFailureHandler SetMemoryFailureHandler(MemoryFailureHandler handler) noexcept
	{
		return s_FailureHandler.exchange(handler, rox::core::MemoryOrderAcqRel);
	}

	[[noreturn]] void ReportMemoryFailure(const MemoryFailure& failure) noexcept
	{
		if (s_bReportingFailure)
		{
			std::_Exit(EXIT_FAILURE);
		}

		s_bReportingFailure = true;
		ASSERT(failure.Resource != nullptr, "A memory failure must identify the responsible memory resource.");
		ASSERT(!failure.ResourceName.empty(), "A memory failure must identify the responsible resource by name.");

		LOG_CRITICAL("Memory", "A mandatory memory allocation failed.");
		FlushLog();

		if (const MemoryFailureHandler handler = s_FailureHandler.load(rox::core::MemoryOrderAcquire))
		{
			handler(failure);
		}

		std::abort();
	}

	void* MemoryResource::Allocate(const MemoryAllocationRequest allocation) noexcept
	{
		ASSERT(IsValidMemoryAlignment(allocation.Alignment), "Memory allocation alignment must be a non-zero power of two.");

		if (allocation.ByteCount == 0)
		{
			return nullptr;
		}

		detail::MemoryTraceScope trace(this);
		void* const memory = allocate(allocation);
		ASSERT(memory == nullptr || (reinterpret_cast<uintptr>(memory) & (allocation.Alignment - 1)) == 0, "A memory resource returned storage that does not satisfy the requested alignment.");
		trace.Allocated(memory, allocation.ByteCount);
		return memory;
	}

	void* MemoryResource::AllocateRequired(const MemoryAllocationRequest allocation) noexcept
	{
		void* const memory = Allocate(allocation);

		if (memory == nullptr && allocation.ByteCount != 0)
		{
			ReportMemoryFailure({EMemoryFailureReason::AllocationFailed, this, GetName(), allocation});
		}

		return memory;
	}

	void MemoryResource::Deallocate(void* const memory, const MemoryAllocationRequest allocation) noexcept
	{
		if (memory == nullptr)
		{
			return;
		}

		ASSERT(IsValidMemoryAlignment(allocation.Alignment), "Memory deallocation alignment must be a non-zero power of two.");
		ASSERT(allocation.ByteCount != 0, "A non-null allocation must have a non-zero byte count.");

		detail::MemoryTraceScope trace(this);
		trace.Deallocating(memory, allocation.ByteCount);
		deallocate(memory, allocation);
	}
} // namespace rox::core
