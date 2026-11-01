#pragma once

#include <IGamepadSource.h>
#include <Result.hpp>

namespace rox::os
{
	// Owner-thread only; polling, moving and destruction use the creating thread.
	// Retains the last published states; XInput requires no owned handle.
	class WindowsGamepadSource final : public IGamepadSource
	{
	public:
		WindowsGamepadSource() noexcept = default;
		~WindowsGamepadSource() noexcept override = default;

		WindowsGamepadSource(const WindowsGamepadSource&) = delete;
		WindowsGamepadSource& operator=(const WindowsGamepadSource&) = delete;
		WindowsGamepadSource(WindowsGamepadSource&&) noexcept = default;
		WindowsGamepadSource& operator=(WindowsGamepadSource&&) noexcept = default;

		// Disconnected slots are successful answers. Any other native failure preserves the published states.
		[[nodiscard]] core::Result<GamepadStateBatch> Poll() override;

	private:
		GamepadState m_States[MAXIMUM_GAMEPAD_SLOTS];
	};
} // namespace rox::os
