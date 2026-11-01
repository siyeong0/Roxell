#include "ShaderCompilerGate.h"

namespace rox::test
{
	void ShaderCompilerGate::Enter()
	{
		std::unique_lock lock(m_Mutex);
		++m_Entries;
		m_Changed.notify_all();
		m_Changed.wait(lock, [&] { return m_bOpen || m_Permits != 0; });
		if (!m_bOpen)
		{
			--m_Permits;
		}
	}

	void ShaderCompilerGate::Open()
	{
		std::lock_guard lock(m_Mutex);
		m_bOpen = true;
		m_Changed.notify_all();
	}

	void ShaderCompilerGate::ReleaseOne()
	{
		std::lock_guard lock(m_Mutex);
		++m_Permits;
		m_Changed.notify_all();
	}

	bool ShaderCompilerGate::WaitForEntries(core::uint32 count, std::chrono::milliseconds timeout)
	{
		std::unique_lock lock(m_Mutex);

		return m_Changed.wait_for(lock, timeout, [&] { return m_Entries >= count; });
	}

	core::uint32 ShaderCompilerGate::GetEntryCount() const
	{
		std::lock_guard lock(m_Mutex);

		return m_Entries;
	}
} // namespace rox::test
