#pragma once

#include <Log.h>
#include <StringView.hpp>

namespace rox::os
{
	// Host-owned, installed before workers start and removed after they stop.
	class WindowsFailureDiagnostics final : public core::IFailureDiagnostics
	{
	public:
		WindowsFailureDiagnostics() noexcept = default;
		~WindowsFailureDiagnostics() override;
		WindowsFailureDiagnostics(const WindowsFailureDiagnostics&) = delete;
		WindowsFailureDiagnostics& operator=(const WindowsFailureDiagnostics&) = delete;

		void Start(core::StringView logPath, bool bRelativeToExecutable) noexcept;
		void Stop() noexcept;
		void Capture(const core::LogEvent& event) noexcept override;

	private:
		void initializeProcess() noexcept;

		wchar_t m_DumpPrefix[1024] = {};
		void* m_Process = nullptr;

		bool m_bInstalled = false;
		bool m_bSymbols = false;
		bool m_bFullMemory = false;
		bool m_bPreviousEnabled = false;

		core::uint32 m_DumpCount = 0;
		bool m_bFatalDumpAttempted = false;
	};
} // namespace rox::os
