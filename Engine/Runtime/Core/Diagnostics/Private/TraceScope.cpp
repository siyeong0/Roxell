#include <TraceScope.h>
#include <TextPrimitives.hpp>
#include <Assertion.h>
#include "TraceInternal.h"
#include "DiagnosticThread.h"

#if ROXELL_ENABLE_PROFILING
namespace rox::core
{

	TraceScope::TraceScope(rox::core::DiagnosticText category, rox::core::DiagnosticText name, ETraceChannel channel) noexcept
	    : m_Sink(activeSink())
	{
		if (m_Sink != nullptr)
		{
			m_Event.CaptureId = queryCaptureId(*m_Sink);

			if (m_Event.CaptureId == 0 || !queryEventEnabled(*m_Sink, ETraceEventType::Scope, channel))
			{
				m_Sink = nullptr;
				return;
			}

			m_PreviousScope = s_ParentScope;
			m_PreviousCapture = s_ParentCapture;
			m_Event.ParentScopeId = s_ParentCapture == m_Event.CaptureId ? s_ParentScope : 0;
			m_Event.ScopeId = nextScopeId();
			m_Event.FrameId = currentFrame();
			s_ParentScope = m_Event.ScopeId;
			s_ParentCapture = m_Event.CaptureId;
			m_Event.Name = name;
			m_Event.Category = category;
			m_Event.TimestampNanoseconds = timestamp();
			m_Event.ThreadId = diagnosticThreadId();
		}
	}

	TraceScope::~TraceScope() noexcept
	{
		if (m_Sink != nullptr)
		{
			ASSERT(m_Event.ThreadId == diagnosticThreadId(), "Trace scopes must end on their construction thread.");
			ASSERT(s_ParentScope == m_Event.ScopeId && s_ParentCapture == m_Event.CaptureId, "Active trace scopes must end in reverse construction order.");

			m_Event.DurationNanoseconds = timestamp() - m_Event.TimestampNanoseconds;
			s_ParentScope = m_PreviousScope;
			s_ParentCapture = m_PreviousCapture;
			dispatch(*m_Sink, m_Event);
		}
	}

}

#endif
