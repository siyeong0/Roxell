#pragma once

#include <Result.hpp>
#include <Span.hpp>
#include <Types.h>

namespace rox::os
{
	// Poll returns every slot in ascending order, including disconnected slots.
	inline constexpr core::uint32 MAXIMUM_GAMEPAD_SLOTS = 4;

	// Reserved XInput bits retain their positions in the button mask.
	inline constexpr core::uint32 GAMEPAD_BUTTON_BIT_COUNT = 16;

	// Stick dead zones use radial magnitude.
	inline constexpr core::int64 GAMEPAD_LEFT_STICK_DEAD_ZONE = 7'849;
	inline constexpr core::int64 GAMEPAD_RIGHT_STICK_DEAD_ZONE = 8'689;
	// Trigger thresholds use scalar magnitude.
	inline constexpr core::int64 GAMEPAD_TRIGGER_THRESHOLD = 30;

	// XInput button bit positions; bits 10 and 11 are reserved and rejected by IsDefinedGamepadButton.
	enum class EGamepadButton : core::uint8
	{
		DPadUp = 0,
		DPadDown = 1,
		DPadLeft = 2,
		DPadRight = 3,
		Start = 4,
		Back = 5,
		LeftThumb = 6,
		RightThumb = 7,
		LeftShoulder = 8,
		RightShoulder = 9,
		A = 12,
		B = 13,
		X = 14,
		Y = 15
	};

	// State array indices: paired stick axes followed by triggers.
	enum class EGamepadAxis : core::uint8
	{
		LeftStickX = 0,
		LeftStickY = 1,
		RightStickX = 2,
		RightStickY = 3,
		LeftTrigger = 4,
		RightTrigger = 5
	};

	inline constexpr core::uint32 MAXIMUM_GAMEPAD_AXES = 6;

	[[nodiscard]] constexpr bool IsDefinedGamepadButton(const EGamepadButton button) noexcept
	{
		switch (button)
		{
		case EGamepadButton::DPadUp:
		case EGamepadButton::DPadDown:
		case EGamepadButton::DPadLeft:
		case EGamepadButton::DPadRight:
		case EGamepadButton::Start:
		case EGamepadButton::Back:
		case EGamepadButton::LeftThumb:
		case EGamepadButton::RightThumb:
		case EGamepadButton::LeftShoulder:
		case EGamepadButton::RightShoulder:
		case EGamepadButton::A:
		case EGamepadButton::B:
		case EGamepadButton::X:
		case EGamepadButton::Y:
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] constexpr bool IsDefinedGamepadAxis(const EGamepadAxis axis) noexcept
	{
		switch (axis)
		{
		case EGamepadAxis::LeftStickX:
		case EGamepadAxis::LeftStickY:
		case EGamepadAxis::RightStickX:
		case EGamepadAxis::RightStickY:
		case EGamepadAxis::LeftTrigger:
		case EGamepadAxis::RightTrigger:
			return true;
		default:
			return false;
		}
	}

	// Raw device levels; consumers own dead zones, normalization and edge detection.
	struct GamepadState final
	{
		bool bConnected = false;

		// XInput button mask; reserved bits do not represent defined buttons.
		core::uint16 Buttons = 0;

		core::int16 LeftX = 0;
		core::int16 LeftY = 0;

		core::int16 RightX = 0;
		core::int16 RightY = 0;

		core::uint8 LeftTrigger = 0;
		core::uint8 RightTrigger = 0;
	};

	// States borrows source storage until the next Poll or source destruction.
	struct GamepadStateBatch final
	{
		core::Span<const GamepadState> States;
	};

	// Owner-thread only; creation, polling and destruction must remain on the same thread.
	class IGamepadSource
	{
	public:
		virtual ~IGamepadSource() = default;

		// Returns every slot in ascending order without waiting for device input.
		// Disconnected slots have bConnected == false; failure means the platform service could not answer.
		[[nodiscard]] virtual core::Result<GamepadStateBatch> Poll() = 0;
	};
} // namespace rox::os
