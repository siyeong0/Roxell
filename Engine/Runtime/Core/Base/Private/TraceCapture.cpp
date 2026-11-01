#include <TraceCapture.h>
#include <Synchronization.hpp>
#include <StringView.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Assertion.h>
#include <Utf8.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace rox::core
{
	namespace
	{
		template <usize Size> uint8 copyLabel(char (&out)[Size], StringView text) noexcept
		{
			usize count = core::Min(text.size(), Size - 1);

			while (count != 0 && !IsValidUtf8(StringView(text.data(), count)))
			{
				--count;
			}

			if (count != 0)
			{
				core::CopyBytes(out, text.data(), count);
			}

			out[count] = '\0';
			return static_cast<uint8>(count);
		}
	}

	TraceCapture::TraceCapture(MemoryResource& resource, usize capacity) noexcept
	    : m_Events(resource)
	    , m_Capacity(capacity)
	{
		ASSERT(capacity > 0, "Trace capture requires positive event capacity.");

		if (!m_Events.Reserve(capacity))
		{
			m_Capacity = 0;
			m_ActiveCapture.store(0, core::MemoryOrderRelaxed);
		}
	}

	void TraceCapture::Write(const TraceEvent& event) noexcept
	{
		ASSERT(IsValidUtf8(StringView(event.Name.data(), event.Name.size())) && IsValidUtf8(StringView(event.Category.data(), event.Category.size())),
		       "Trace labels must be valid UTF-8.");
		ASSERT(event.Type >= ETraceEventType::Scope && event.Type <= ETraceEventType::GpuDuration,
		       "Trace event type must be supported.");
		ASSERT(event.Type != ETraceEventType::Counter || core::IsFinite(event.Value),
		       "Trace counters require a finite value.");

		core::LockGuard lock(m_Mutex);

		if (event.CaptureId != m_ActiveCapture.load(core::MemoryOrderRelaxed) || event.CaptureId == 0)
		{
			return;
		}

		if (m_Events.Size() == m_Capacity)
		{
			if (m_Dropped != NumericLimits<uint64>::max())
			{
				++m_Dropped;
			}

			return;
		}

		auto& stored = m_Events.EmplaceRequired();
		stored.Type = event.Type;
		stored.NameBytes = copyLabel(stored.Name, event.Name);
		stored.CategoryBytes = copyLabel(stored.Category, event.Category);
		stored.bTruncated = stored.NameBytes != event.Name.size() || stored.CategoryBytes != event.Category.size();
		stored.TimestampNanoseconds = event.TimestampNanoseconds;
		stored.DurationNanoseconds = event.DurationNanoseconds;
		stored.ThreadId = event.ThreadId;
		stored.Value = event.Value;
		stored.ResourceId = event.ResourceId;
		stored.Address = event.Address;
		stored.SizeBytes = event.SizeBytes;
		stored.CaptureId = event.CaptureId;
		stored.ScopeId = event.ScopeId;
		stored.ParentScopeId = event.ParentScopeId;
		stored.FrameId = event.FrameId;
		stored.Queue = event.Queue;
	}

	Span<const CapturedTraceEvent> TraceCapture::GetEvents() const noexcept
	{
		return {m_Events.Data(), m_Events.Size()};
	}

	void TraceCapture::Reset() noexcept
	{
		(void)BeginCapture();
	}

	uint64 TraceCapture::BeginCapture() noexcept
	{
		core::LockGuard lock(m_Mutex);
		if (m_Capacity == 0)
		{
			return 0;
		}

		ASSERT(m_Generation != NumericLimits<uint64>::max(), "Capture generations must remain representable.");

		m_Events.Clear();
		m_Dropped = 0;
		++m_Generation;
		m_ActiveCapture.store(m_Generation, core::MemoryOrderRelease);

		return m_Generation;
	}

	void TraceCapture::StopCapture() noexcept
	{
		core::LockGuard lock(m_Mutex);
		m_ActiveCapture.store(0, core::MemoryOrderRelease);
	}

	uint64 TraceCapture::GetDroppedCount() const noexcept
	{
		core::LockGuard lock(m_Mutex);
		return m_Dropped;
	}

	bool TraceCapture::TryCopySnapshot(TraceSnapshot& output) const noexcept
	{
		// Reserve before taking the lock: allocation itself may emit memory events.
		if (!output.Events.Reserve(m_Capacity))
		{
			return false;
		}

		core::LockGuard lock(m_Mutex);
		output.Events.Clear();

		for (const auto& event : m_Events)
		{
			output.Events.AddRequired(event);
		}

		output.CaptureId = m_Generation;
		output.Dropped = m_Dropped;
		output.bRecording = m_ActiveCapture.load(core::MemoryOrderRelaxed) != 0;

		return true;
	}
}
