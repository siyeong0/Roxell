#pragma once

#include <IWindowSystem.h>
#include <MemoryResource.h>

namespace rox::os
{
	struct WindowsWindowSystemOptions final
	{
		core::uint32 MaximumWindows = DEFAULT_MAXIMUM_WINDOW_COUNT;
		core::uint32 MaximumEventsPerPoll = DEFAULT_MAXIMUM_WINDOW_EVENT_COUNT;
	};

	class WindowsWindowSystem final : public IWindowSystem
	{
	public:
		WindowsWindowSystem() noexcept = default;
		~WindowsWindowSystem() noexcept override;
		WindowsWindowSystem(const WindowsWindowSystem&) = delete;
		WindowsWindowSystem& operator=(const WindowsWindowSystem&) = delete;
		WindowsWindowSystem(WindowsWindowSystem&&) = delete;
		WindowsWindowSystem& operator=(WindowsWindowSystem&&) = delete;

		[[nodiscard]] static core::Result<void> TryCreate(core::MemoryResource& resource, WindowsWindowSystemOptions options, WindowsWindowSystem* outSystem);

		[[nodiscard]] core::Result<WindowId> CreateWindow(const WindowDesc& desc) override;
		[[nodiscard]] core::Result<void> DestroyWindow(WindowId window) override;
		[[nodiscard]] core::Result<void> SetWindowVisibility(WindowId window, bool bVisible) override;
		[[nodiscard]] core::Result<void> ResizeWindow(WindowId window, WindowExtent clientExtent) override;
		[[nodiscard]] core::Result<void> SetPointerMode(WindowId window, EPointerMode mode) override;

		[[nodiscard]] core::Result<WindowState> GetWindowState(WindowId window) const override;
		[[nodiscard]] core::Result<NativeWindowHandle> GetNativeHandle(WindowId window) const override;

		[[nodiscard]] WindowEventBatch PollEvents() override;

		[[nodiscard]] core::Result<void> SetTextInputState(WindowId window, const WindowTextInputState& state) override;
		[[nodiscard]] core::Result<void> PublishTextDocument(WindowId window, const WindowTextDocument& document) override;

		[[nodiscard]] core::Result<core::String> ReadClipboardText(WindowId window, core::MemoryResource& memory, core::uint32 maximumBytes) override;
		[[nodiscard]] core::Result<void> WriteClipboardText(WindowId window, core::StringView text) override;

		core::Result<void> SetAccessibilitySnapshot(WindowId window, const AccessibilitySnapshot& snapshot) override;
		core::Result<AccessibilityRequest> PollAccessibilityAction(WindowId window) override;

		[[nodiscard]] bool IsValid() const noexcept;

	private:
		class State;

		void assertInvariant() const noexcept;

	private:
		core::MemoryResource* m_Resource = nullptr;
		State* m_State = nullptr;
	};
} // namespace rox::os
