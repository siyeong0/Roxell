#pragma once

#include <IFileWatcher.h>
#include <MemoryResource.h>
#include <StringView.hpp>
#include <Types.h>

namespace rox::os
{
	struct WindowsFileWatcherOptions final
	{
		core::uint32 MaximumChangesPerPoll = DEFAULT_MAXIMUM_FILE_CHANGE_COUNT;
		rox::core::usize NativeBufferBytes = DEFAULT_FILE_WATCH_BUFFER_BYTES;
	};

	// Move-only recursive watch; create, poll, move and destroy on the same thread.
	class WindowsFileWatcher final : public IFileWatcher
	{
	public:
		WindowsFileWatcher() noexcept = default;
		~WindowsFileWatcher() noexcept override;

		WindowsFileWatcher(const WindowsFileWatcher&) = delete;
		WindowsFileWatcher& operator=(const WindowsFileWatcher&) = delete;
		WindowsFileWatcher(WindowsFileWatcher&& other) noexcept;
		WindowsFileWatcher& operator=(WindowsFileWatcher&& other) noexcept;

		// The resource outlives the watcher and its batches. The UTF-8 root is passed to Win32 without a long-path prefix.
		// Creation arms the first native read before returning.
		[[nodiscard]] static core::Result<WindowsFileWatcher> Create(core::MemoryResource& resource, core::StringView rootPathUtf8, WindowsFileWatcherOptions options = {});

		[[nodiscard]] core::Result<FileChangeBatch> Poll() override;
		[[nodiscard]] bool IsValid() const noexcept;

	private:
		// Hides native handles and overlapped I/O state.
		class State;

		WindowsFileWatcher(core::MemoryResource& resource, State* state) noexcept;

	private:
		core::MemoryResource* m_Resource = nullptr;
		State* m_State = nullptr;
	};
} // namespace rox::os
