#pragma once

#include <condition_variable>
#include <mutex>
#include <Types.h>

#include <chrono>

namespace rox::test
{
	class ShaderCompilerGate final
	{
	public:
		void Enter();
		void Open();
		void ReleaseOne();
		bool WaitForEntries(core::uint32 count, std::chrono::milliseconds timeout);
		core::uint32 GetEntryCount() const;

	private:
		mutable std::mutex m_Mutex;
		std::condition_variable m_Changed;
		core::uint32 m_Entries = 0;
		core::uint32 m_Permits = 0;
		bool m_bOpen = false;
	};

} // namespace rox::test
