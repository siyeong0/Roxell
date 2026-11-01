#pragma once

#include <Log.h>
#include <StringView.hpp>
#include <Synchronization.hpp>

namespace rox::os
{
	// Core log output to the Windows debugger and file. Install at startup; remove before destruction.
	class WindowsLogSink final : public core::ILogSink
	{
	public:
		WindowsLogSink() noexcept = default;
		~WindowsLogSink() override;

		WindowsLogSink(const WindowsLogSink&) = delete;
		WindowsLogSink& operator=(const WindowsLogSink&) = delete;
		WindowsLogSink(WindowsLogSink&&) = delete;
		WindowsLogSink& operator=(WindowsLogSink&&) = delete;

		[[nodiscard]] bool TryOpen(core::StringView utf8Path) noexcept;

		// Creates bounded physical parent directories; rejects reparse points, alternate streams and ambiguous trailing characters.
		[[nodiscard]] bool TryOpenCreatingDirectories(core::StringView utf8Path) noexcept;

		// Appends to a path relative to the running executable, creating parent directories.
		// Returns false if resolving the executable path, creating directories or opening the file fails.
		[[nodiscard]] bool TryOpenRelativeToExecutable(core::StringView utf8RelativePath) noexcept;
		void Close() noexcept;
		[[nodiscard]] bool IsOpen() const noexcept;
		[[nodiscard]] bool HasWriteFailure() const noexcept;

		void Write(const core::LogEvent& event, rox::core::DiagnosticText jsonLine) noexcept override;
		void Flush() noexcept override;

	private:
		void lock() noexcept;
		void unlock() noexcept;

	private:
		void* m_FileHandle = nullptr;

		rox::core::AtomicFlag m_OutputLock = ATOMIC_FLAG_INIT;
		rox::core::Atomic<bool> m_bWriteFailed = false;
	};
} // namespace rox::os
