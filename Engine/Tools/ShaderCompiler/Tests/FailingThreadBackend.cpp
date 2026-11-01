#include "FailingThreadBackend.h"

namespace rox::test
{
	FailingThreadBackend::FailingThreadBackend(core::IThreadBackend& backend, core::uint32 failureIndex) noexcept
		: m_Backend(backend)
		, m_FailureIndex(failureIndex)
	{
	}

	core::NativeThreadStartResult FailingThreadBackend::Start(core::NativeThreadEntry& entry) noexcept
	{
		if (++m_StartCount == m_FailureIndex)
		{
			return {};
		}

		return m_Backend.Start(entry);
	}

	void FailingThreadBackend::Join(core::NativeThread thread) noexcept
	{
		m_Backend.Join(thread);
		++m_JoinedCount;
	}

	core::ThreadId FailingThreadBackend::CurrentThreadId() const noexcept
	{
		return m_Backend.CurrentThreadId();
	}

	core::uint32 FailingThreadBackend::GetJoinedCount() const noexcept
	{
		return m_JoinedCount;
	}
} // namespace rox::test
