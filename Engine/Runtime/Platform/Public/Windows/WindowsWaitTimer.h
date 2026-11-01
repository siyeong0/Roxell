#pragma once

#include <Result.hpp>
#include <Types.h>

namespace rox::os
{
	// One owner thread arms and waits on this timer; it never changes system timer resolution.
	class WindowsWaitTimer final
	{
	public:
		static core::Result<WindowsWaitTimer> Create();
		WindowsWaitTimer(WindowsWaitTimer&& other) noexcept;
		WindowsWaitTimer(const WindowsWaitTimer&) = delete;
		WindowsWaitTimer& operator=(const WindowsWaitTimer&) = delete;
		~WindowsWaitTimer();

		core::Result<void> Wait(core::uint32 milliseconds);

	private:
		explicit WindowsWaitTimer(void* handle) noexcept
			: m_Handle(handle)
		{
		}

		void* m_Handle = nullptr;
	};
}
