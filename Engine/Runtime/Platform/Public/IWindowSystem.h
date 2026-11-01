#pragma once

#include <Accessibility.h>
#include <NativeWindowHandle.h>
#include <Result.hpp>
#include <WindowTextDocument.h>
#include <WindowTypes.h>

namespace rox::os
{
	class IWindowSystem
	{
	public:
		virtual ~IWindowSystem() = default;

		[[nodiscard]] virtual core::Result<WindowId> CreateWindow(const WindowDesc& desc) = 0;
		[[nodiscard]] virtual core::Result<void> DestroyWindow(WindowId window) = 0;
		[[nodiscard]] virtual core::Result<void> SetWindowVisibility(WindowId window, bool bVisible) = 0;
		[[nodiscard]] virtual core::Result<void> ResizeWindow(WindowId window, WindowExtent clientExtent) = 0;

		// One verb and not three. A visibility knob, a confinement knob, and a raw-input knob would be
		// three switches with one legal combination each way, and the combination is the mode.
		[[nodiscard]] virtual core::Result<void> SetPointerMode(WindowId window, EPointerMode mode) = 0;
		[[nodiscard]] virtual core::Result<WindowState> GetWindowState(WindowId window) const = 0;
		[[nodiscard]] virtual core::Result<NativeWindowHandle> GetNativeHandle(WindowId window) const = 0;

		// With an active native text document, a backend may end a batch at an input
		// publication boundary. Apply the batch and republish text/selection before
		// polling again; later native keys must observe the updated caret.
		[[nodiscard]] virtual WindowEventBatch PollEvents() = 0;
		[[nodiscard]] virtual core::Result<void> SetAccessibilitySnapshot(WindowId, const AccessibilitySnapshot&)
		{
			return core::Result<void>::Failure(core::EErrorCode::Unsupported, "Platform.Accessibility", "This window backend has no accessibility provider.");
		}

		[[nodiscard]] virtual core::Result<AccessibilityRequest> PollAccessibilityAction(WindowId)
		{
			return core::Result<AccessibilityRequest>::Success({});
		}

		[[nodiscard]] virtual core::Result<core::String> ReadClipboardText(WindowId, core::MemoryResource&, core::uint32)
		{
			return core::Result<core::String>::Failure(core::EErrorCode::Unsupported, "platform.window.clipboard", "This window backend has no clipboard adapter.");
		}

		[[nodiscard]] virtual core::Result<void> WriteClipboardText(WindowId, core::StringView)
		{
			return core::Result<void>::Failure(core::EErrorCode::Unsupported, "platform.window.clipboard", "This window backend has no clipboard adapter.");
		}
		// Physical client pixels; Session changes cancel the previous composition before activation.
		[[nodiscard]] virtual core::Result<void> PublishTextDocument(WindowId, const WindowTextDocument&)
		{
			return core::Result<void>::Failure(core::EErrorCode::Unsupported, "platform.window.text", "This window backend has no retained native text document.");
		}

		[[nodiscard]] virtual core::Result<void> SetTextInputState(WindowId, const WindowTextInputState&)
		{
			return core::Result<void>::Failure(core::EErrorCode::Unsupported, "platform.window.text", "This window backend has no native text input adapter.");
		}
	};
} // namespace rox::os
