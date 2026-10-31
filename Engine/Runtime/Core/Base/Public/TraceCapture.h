#pragma once

#include <Array.hpp>
#include <Synchronization.hpp>
#include <StringView.hpp>
#include <Types.h>
#include <Span.hpp>
#include <Trace.h>
#include <mutex>
#include <atomic>

namespace rox::core
{
	struct CapturedTraceEvent final
	{
		ETraceEventType Type = ETraceEventType::Scope;

		char Name[128]{};
		char Category[64]{};
		rox::core::uint8 NameBytes = 0;
		rox::core::uint8 CategoryBytes = 0;
		bool bTruncated = false;

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

	using TraceWriteCallback = bool (*)(void* context, rox::core::StringView bytes) noexcept;

	struct TraceCaptureMetadata final
	{
		char Build[128]{};
		char Backend[64]{};
		char Device[128]{};
		char Quality[128]{};

		rox::core::uint32 Width = 0;
		rox::core::uint32 Height = 0;
	};

	struct TraceSnapshot final
	{
		explicit TraceSnapshot(MemoryResource& resource) noexcept : Events(resource) {}

		Array<CapturedTraceEvent> Events;
		TraceCaptureMetadata Metadata;

		rox::core::uint64 CaptureId = 0;
		rox::core::uint64 Dropped = 0;

		bool bRecording = false;
	};

	struct TraceScopeStatistics final
	{
		rox::core::usize EventIndex = 0;

		rox::core::uint64 InclusiveNanoseconds = 0;
		rox::core::uint64 ExclusiveNanoseconds = 0;

		// Missing parent or ancestor in the retained records; capture-wide loss is reported by Dropped.
		bool bIncomplete = false;
		rox::core::uint32 Depth = 0;
	};

	// The v1 binary format uses explicit little-endian fields and nanoseconds, never native struct bytes.
	// Writer must be non-null; callback or label validation failure returns false and may leave partial output.
	bool WriteTraceCapture(const TraceSnapshot& snapshot, TraceWriteCallback writer, void* context) noexcept;

	// Invalid data leaves output unchanged. MaximumEvents bounds allocations before decoding.
	bool ReadTraceCapture(rox::core::StringView bytes, TraceSnapshot& output, rox::core::usize maximumEvents) noexcept;
	bool AnalyzeTraceSnapshot(const TraceSnapshot& snapshot, Array<TraceScopeStatistics>& output) noexcept;

	// The resource and sink outlive callbacks/scopes. Install/detach only when emitters are quiescent.
	// Begin/Stop and snapshot copying are synchronized; borrowed GetEvents requires stopped collection.
	class TraceCapture final : public ITraceSink
	{
	public:
		// Allocation failure leaves Capacity()==0 and recording disabled.
		TraceCapture(MemoryResource& resource, rox::core::usize capacity) noexcept;
		TraceCapture(const TraceCapture&) = delete;
		TraceCapture& operator=(const TraceCapture&) = delete;

		void Write(const TraceEvent& event) noexcept override;
		rox::core::uint64 GetCaptureId() const noexcept override { return m_ActiveCapture.load(rox::core::MemoryOrderAcquire); }

		rox::core::uint64 BeginCapture() noexcept;
		void StopCapture() noexcept;
		bool IsRecording() const noexcept { return GetCaptureId() != 0; }

		void SetMemoryCaptureEnabled(bool enabled) noexcept { m_bMemoryEnabled.store(enabled, rox::core::MemoryOrderRelease); }
		void SetDeepCaptureEnabled(bool enabled) noexcept { m_bDeepEnabled.store(enabled, rox::core::MemoryOrderRelease); }

		bool IsChannelEnabled(ETraceChannel channel) const noexcept override
		{
			return channel == ETraceChannel::Default || m_bDeepEnabled.load(rox::core::MemoryOrderAcquire);
		}

		bool IsEventEnabled(ETraceEventType type) const noexcept override
		{
			return (type != ETraceEventType::Allocation && type != ETraceEventType::Deallocation) ||
			       m_bMemoryEnabled.load(rox::core::MemoryOrderAcquire);
		}

		bool TryCopySnapshot(TraceSnapshot& output) const noexcept;
		Span<const CapturedTraceEvent> GetEvents() const noexcept;

		rox::core::uint64 GetDroppedCount() const noexcept;

		rox::core::usize Capacity() const noexcept { return m_Capacity; }

		void Reset() noexcept;

		// The callback borrows each byte range for one call. False stops export; discard any partial artifact.
		bool WriteChromeTrace(TraceWriteCallback writer, void* context) const noexcept;

	private:
		Array<CapturedTraceEvent> m_Events;
		rox::core::usize m_Capacity;
		rox::core::uint64 m_Dropped = 0;

		mutable rox::core::Mutex m_Mutex;

		rox::core::Atomic<rox::core::uint64> m_ActiveCapture = 1;
		rox::core::uint64 m_Generation = 1;

		rox::core::Atomic<bool> m_bMemoryEnabled = true;
		rox::core::Atomic<bool> m_bDeepEnabled = true;
	};
}
