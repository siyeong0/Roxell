#pragma once

#include <Accessibility.h>
#include <UniquePtr.hpp>

namespace rox::os
{
	struct WindowsAccessibilityState;

	class WindowsAccessibility final
	{
	public:
		explicit WindowsAccessibility(core::SharedPtr<WindowsAccessibilityState> state) noexcept;
		~WindowsAccessibility();

		static core::Result<core::UniquePtr<WindowsAccessibility>> Create(void* window);

		core::Result<void> Publish(const AccessibilitySnapshot& snapshot);
		AccessibilityRequest Poll();
		core::int64 ReturnProvider(core::uint64 word, core::int64 data);
		void Close() noexcept;

	private:
		core::SharedPtr<WindowsAccessibilityState> m_State;
	};
}
