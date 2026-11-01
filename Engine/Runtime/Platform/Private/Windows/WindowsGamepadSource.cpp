#include <WindowsGamepadSource.h>
#include <Types.h>
#include <Assertion.h>

#include <Utility.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>

namespace rox::os
{
	using namespace core;

	namespace
	{
		// The button mask this backend publishes is XInput's own wButtons, so the two
		// vocabularies must agree bit for bit. Pinned as literals against the SDK macros
		// rather than derived from them, so a mask that moved on either side is caught
		// here instead of silently remapping every authored gamepad source.
		static_assert(XINPUT_GAMEPAD_DPAD_UP == 1u << static_cast<unsigned>(EGamepadButton::DPadUp));
		static_assert(XINPUT_GAMEPAD_DPAD_DOWN == 1u << static_cast<unsigned>(EGamepadButton::DPadDown));
		static_assert(XINPUT_GAMEPAD_DPAD_LEFT == 1u << static_cast<unsigned>(EGamepadButton::DPadLeft));
		static_assert(XINPUT_GAMEPAD_DPAD_RIGHT == 1u << static_cast<unsigned>(EGamepadButton::DPadRight));
		static_assert(XINPUT_GAMEPAD_START == 1u << static_cast<unsigned>(EGamepadButton::Start));
		static_assert(XINPUT_GAMEPAD_BACK == 1u << static_cast<unsigned>(EGamepadButton::Back));
		static_assert(XINPUT_GAMEPAD_LEFT_THUMB == 1u << static_cast<unsigned>(EGamepadButton::LeftThumb));
		static_assert(XINPUT_GAMEPAD_RIGHT_THUMB == 1u << static_cast<unsigned>(EGamepadButton::RightThumb));
		static_assert(XINPUT_GAMEPAD_LEFT_SHOULDER == 1u << static_cast<unsigned>(EGamepadButton::LeftShoulder));
		static_assert(XINPUT_GAMEPAD_RIGHT_SHOULDER == 1u << static_cast<unsigned>(EGamepadButton::RightShoulder));
		static_assert(XINPUT_GAMEPAD_A == 1u << static_cast<unsigned>(EGamepadButton::A));
		static_assert(XINPUT_GAMEPAD_B == 1u << static_cast<unsigned>(EGamepadButton::B));
		static_assert(XINPUT_GAMEPAD_X == 1u << static_cast<unsigned>(EGamepadButton::X));
		static_assert(XINPUT_GAMEPAD_Y == 1u << static_cast<unsigned>(EGamepadButton::Y));

		// The two stick thresholds and the trigger threshold this repository states are
		// the ones XInput documents, pinned against the SDK macros at the one place both
		// vocabularies are visible. A transcription error in the SDK-neutral header is
		// otherwise invisible to every contract, because no pad can be attached here.
		static_assert(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE == GAMEPAD_LEFT_STICK_DEAD_ZONE);
		static_assert(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE == GAMEPAD_RIGHT_STICK_DEAD_ZONE);
		static_assert(XINPUT_GAMEPAD_TRIGGER_THRESHOLD == GAMEPAD_TRIGGER_THRESHOLD);

		static_assert(XUSER_MAX_COUNT == MAXIMUM_GAMEPAD_SLOTS, "The slot bound must be the XInput user-index bound.");
	} // namespace

	Result<GamepadStateBatch> WindowsGamepadSource::Poll()
	{
		// The batch is published only after every slot has answered, so a failure leaves
		// the previously published states exactly as they were.
		GamepadState sampled[MAXIMUM_GAMEPAD_SLOTS];

		for (uint32 slot = 0; slot < MAXIMUM_GAMEPAD_SLOTS; ++slot)
		{
			XINPUT_STATE native = {};
			const DWORD status = ::XInputGetState(static_cast<DWORD>(slot), &native);

			if (status == ERROR_DEVICE_NOT_CONNECTED)
			{
				// Not a failure: an empty slot is the ordinary state of a shipping
				// machine, and a default GamepadState already says so.
				continue;
			}

			if (status != ERROR_SUCCESS)
			{
				return Result<GamepadStateBatch>::Failure(EErrorCode::ExternalFailure, "ROX-PLATFORM-GAMEPAD-POLL",
					"The Windows gamepad service refused to report the state of a slot.",
					"Poll the gamepad source again on the next iteration.");
			}

			GamepadState& state = sampled[slot];
			state.bConnected = true;
			// wButtons is published verbatim: EGamepadButton IS the bit position, which
			// the static assertions above pin, so no remapping table exists to drift.
			state.Buttons = static_cast<uint16>(native.Gamepad.wButtons);
			state.LeftX = static_cast<int16>(native.Gamepad.sThumbLX);
			state.LeftY = static_cast<int16>(native.Gamepad.sThumbLY);
			state.RightX = static_cast<int16>(native.Gamepad.sThumbRX);
			state.RightY = static_cast<int16>(native.Gamepad.sThumbRY);
			state.LeftTrigger = static_cast<uint8>(native.Gamepad.bLeftTrigger);
			state.RightTrigger = static_cast<uint8>(native.Gamepad.bRightTrigger);
			// dwPacketNumber is READ and discarded, and that is the decision rather than
			// an omission: an edge is the consumer's comparison against its own held set,
			// so a packet number would be a second and weaker copy of it.
		}

		for (uint32 slot = 0; slot < MAXIMUM_GAMEPAD_SLOTS; ++slot)
		{
			m_States[slot] = sampled[slot];
		}

		GamepadStateBatch batch;
		batch.States = Span<const GamepadState>(m_States, static_cast<usize>(MAXIMUM_GAMEPAD_SLOTS));
		ASSERT(batch.States.Size() == static_cast<usize>(MAXIMUM_GAMEPAD_SLOTS), "A published gamepad batch must carry one state per slot.");
		return Result<GamepadStateBatch>::Success(batch);
	}
} // namespace rox::os
