#pragma once

#include <cstddef>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <cstdint>
#include <string_view>

#ifndef ROXELL_ENABLE_PROFILING
#define ROXELL_ENABLE_PROFILING 1
#endif

namespace rox::core
{
	enum class ETraceChannel
	{
		Default,
		Deep
	};

	enum class ETraceEventType
	{
		Scope,
		Counter,
		Allocation,
		Deallocation,
		Frame,
		GpuDuration
	};

	struct TraceEvent final
	{
		ETraceEventType Type = ETraceEventType::Scope;

		rox::core::DiagnosticText Name;
		rox::core::DiagnosticText Category;

		rox::core::uint64 TimestampNanoseconds = 0;
		rox::core::uint64 DurationNanoseconds = 0;
		rox::core::uint64 ThreadId = 0;

		double Value = 0;

		rox::core::uintptr ResourceId = 0;
		rox::core::uintptr Address = 0;
		rox::core::usize SizeBytes = 0;

		rox::core::uint64 CaptureId = 1;
		rox::core::uint64 ScopeId = 0;
		rox::core::uint64 ParentScopeId = 0;

		rox::core::uint64 FrameId = 0;
		rox::core::uint32 Queue = 0;
	};

}
