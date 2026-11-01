#pragma once

#include <Trace.h>
#include <Types.h>

namespace rox::core::detail
{
	// Only the outer logical resource operation emits; upstream wrapper calls share its lifetime.
	class MemoryTraceScope final
	{
#if ROXELL_ENABLE_PROFILING
	public:
		explicit MemoryTraceScope(const void* resource) noexcept
		    : m_Resource(resource)
		    , m_bOuter(!s_bActive && IsTracingEnabled())
		{
			if (m_bOuter)
			{
				s_bActive = true;
			}
		}

		~MemoryTraceScope() noexcept
		{
			if (m_bOuter)
			{
				s_bActive = false;
			}
		}

		MemoryTraceScope(const MemoryTraceScope&) = delete;
		MemoryTraceScope& operator=(const MemoryTraceScope&) = delete;

		void Allocated(const void* address, usize bytes) noexcept
		{
			if (m_bOuter)
			{
				TraceAllocation(m_Resource, address, bytes);
			}
		}

		void Deallocating(const void* address, usize bytes) noexcept
		{
			if (m_bOuter)
			{
				TraceDeallocation(m_Resource, address, bytes);
			}
		}

	private:
		inline static thread_local bool s_bActive = false;

		const void* m_Resource;
		bool m_bOuter;
#else
	public:
		explicit MemoryTraceScope(const void*) noexcept {}
		void Allocated(const void*, usize) noexcept {}
		void Deallocating(const void*, usize) noexcept {}
#endif
	};
}
