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

		DiagnosticText Name;
		DiagnosticText Category;

		uint64 TimestampNanoseconds = 0;
		uint64 DurationNanoseconds = 0;
		uint64 ThreadId = 0;

		double Value = 0;

		uintptr ResourceId = 0;
		uintptr Address = 0;
		usize SizeBytes = 0;

		uint64 CaptureId = 1;
		uint64 ScopeId = 0;
		uint64 ParentScopeId = 0;

		uint64 FrameId = 0;
		uint32 Queue = 0;
	};

}
