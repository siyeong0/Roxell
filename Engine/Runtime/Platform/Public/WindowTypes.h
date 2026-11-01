#pragma once

#include <ComparisonTypes.hpp>
#include <Span.hpp>
#include <StringView.hpp>
#include <TypeTraits.hpp>
#include <Types.h>

#include <Utility.hpp>
#include <Vocabulary.hpp>

namespace rox::os
{
	constexpr core::uint32 DEFAULT_MAXIMUM_WINDOW_COUNT = 8;
	constexpr core::uint32 MAXIMUM_WINDOW_COUNT = 64;
	constexpr core::uint32 DEFAULT_MAXIMUM_WINDOW_EVENT_COUNT = 4'096;
	constexpr core::uint32 MAXIMUM_WINDOW_EVENT_COUNT = 65'536;
	constexpr core::uint32 MAXIMUM_WINDOW_EXTENT_DIMENSION = 16'384;
	constexpr rox::core::usize MAXIMUM_WINDOW_TITLE_UTF8_BYTE_COUNT = 1'024;

	struct WindowId final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const WindowId&) const = default;
	};

	struct WindowExtent final
	{
		core::uint32 Width = 0;
		core::uint32 Height = 0;

		core::StrongOrdering operator<=>(const WindowExtent&) const = default;
	};

	struct WindowDesc final
	{
		core::StringView Title;
		WindowExtent ClientExtent = { 1'280, 720 };
		bool bVisible = false;
		bool bResizable = true;
	};

	enum class EPointerMode : core::uint8
	{
		// A visible, unconfined cursor with client-coordinate PointerMoved events.
		Free,
		// A hidden, confined cursor with PointerRelativeMoved events; client-coordinate events continue.
		Relative
	};

	// The cursor-confinement state of the process-global relative-pointer arbiter. Stale names a
	// rectangle this process applied and a move, a resize, or a DPI change has since invalidated,
	// which a two-valued model cannot express and would silently skip re-applying.
	enum class EConfinement : core::uint8
	{
		Absent,
		Current,
		Stale
	};

	struct WindowState final
	{
		WindowExtent ClientExtent;
		core::uint32 DpiX = 96;
		core::uint32 DpiY = 96;

		// The requested pointer mode. It is the only pointer state a window record holds and it
		// never changes by itself.
		EPointerMode PointerMode = EPointerMode::Free;
		bool bFocused = false;
		bool bMinimized = false;
		bool bVisible = false;

		// The effective answer, derived from the arbiter record and from nothing else: whether
		// relative reports are being delivered to this window.
		bool bPointerRelativeActive = false;
	};

	// Values match the USB HID keyboard/keypad usage page for stable physical-key identity.
	enum class EPhysicalKey : core::uint16
	{
		Unknown = 0x00,
		A = 0x04,
		B = 0x05,
		C = 0x06,
		D = 0x07,
		E = 0x08,
		F = 0x09,
		G = 0x0A,
		H = 0x0B,
		I = 0x0C,
		J = 0x0D,
		K = 0x0E,
		L = 0x0F,
		M = 0x10,
		N = 0x11,
		O = 0x12,
		P = 0x13,
		Q = 0x14,
		R = 0x15,
		S = 0x16,
		T = 0x17,
		U = 0x18,
		V = 0x19,
		W = 0x1A,
		X = 0x1B,
		Y = 0x1C,
		Z = 0x1D,
		Digit1 = 0x1E,
		Digit2 = 0x1F,
		Digit3 = 0x20,
		Digit4 = 0x21,
		Digit5 = 0x22,
		Digit6 = 0x23,
		Digit7 = 0x24,
		Digit8 = 0x25,
		Digit9 = 0x26,
		Digit0 = 0x27,
		Enter = 0x28,
		Escape = 0x29,
		Backspace = 0x2A,
		Tab = 0x2B,
		Space = 0x2C,
		Minus = 0x2D,
		Equal = 0x2E,
		LeftBracket = 0x2F,
		RightBracket = 0x30,
		Backslash = 0x31,
		NonUsHash = 0x32,
		Semicolon = 0x33,
		Apostrophe = 0x34,
		Grave = 0x35,
		Comma = 0x36,
		Period = 0x37,
		Slash = 0x38,
		CapsLock = 0x39,
		F1 = 0x3A,
		F2 = 0x3B,
		F3 = 0x3C,
		F4 = 0x3D,
		F5 = 0x3E,
		F6 = 0x3F,
		F7 = 0x40,
		F8 = 0x41,
		F9 = 0x42,
		F10 = 0x43,
		F11 = 0x44,
		F12 = 0x45,
		PrintScreen = 0x46,
		ScrollLock = 0x47,
		Pause = 0x48,
		Insert = 0x49,
		Home = 0x4A,
		PageUp = 0x4B,
		Delete = 0x4C,
		End = 0x4D,
		PageDown = 0x4E,
		Right = 0x4F,
		Left = 0x50,
		Down = 0x51,
		Up = 0x52,
		NumLock = 0x53,
		KeypadDivide = 0x54,
		KeypadMultiply = 0x55,
		KeypadSubtract = 0x56,
		KeypadAdd = 0x57,
		KeypadEnter = 0x58,
		Keypad1 = 0x59,
		Keypad2 = 0x5A,
		Keypad3 = 0x5B,
		Keypad4 = 0x5C,
		Keypad5 = 0x5D,
		Keypad6 = 0x5E,
		Keypad7 = 0x5F,
		Keypad8 = 0x60,
		Keypad9 = 0x61,
		Keypad0 = 0x62,
		KeypadDecimal = 0x63,
		NonUsBackslash = 0x64,
		Application = 0x65,
		LeftControl = 0xE0,
		LeftShift = 0xE1,
		LeftAlt = 0xE2,
		LeftMeta = 0xE3,
		RightControl = 0xE4,
		RightShift = 0xE5,
		RightAlt = 0xE6,
		RightMeta = 0xE7
	};

	enum class EPointerButton : core::uint8
	{
		Left,
		Right,
		Middle,
		X1,
		X2
	};

	// Physical keys are sparse; range checks would admit undefined keys.
	// Unknown is an absent-key sentinel and cannot be bound as a physical key.
	[[nodiscard]] constexpr bool IsDefinedPhysicalKey(const EPhysicalKey key) noexcept
	{
		switch (key)
		{
		case EPhysicalKey::A:
		case EPhysicalKey::B:
		case EPhysicalKey::C:
		case EPhysicalKey::D:
		case EPhysicalKey::E:
		case EPhysicalKey::F:
		case EPhysicalKey::G:
		case EPhysicalKey::H:
		case EPhysicalKey::I:
		case EPhysicalKey::J:
		case EPhysicalKey::K:
		case EPhysicalKey::L:
		case EPhysicalKey::M:
		case EPhysicalKey::N:
		case EPhysicalKey::O:
		case EPhysicalKey::P:
		case EPhysicalKey::Q:
		case EPhysicalKey::R:
		case EPhysicalKey::S:
		case EPhysicalKey::T:
		case EPhysicalKey::U:
		case EPhysicalKey::V:
		case EPhysicalKey::W:
		case EPhysicalKey::X:
		case EPhysicalKey::Y:
		case EPhysicalKey::Z:
		case EPhysicalKey::Digit1:
		case EPhysicalKey::Digit2:
		case EPhysicalKey::Digit3:
		case EPhysicalKey::Digit4:
		case EPhysicalKey::Digit5:
		case EPhysicalKey::Digit6:
		case EPhysicalKey::Digit7:
		case EPhysicalKey::Digit8:
		case EPhysicalKey::Digit9:
		case EPhysicalKey::Digit0:
		case EPhysicalKey::Enter:
		case EPhysicalKey::Escape:
		case EPhysicalKey::Backspace:
		case EPhysicalKey::Tab:
		case EPhysicalKey::Space:
		case EPhysicalKey::Minus:
		case EPhysicalKey::Equal:
		case EPhysicalKey::LeftBracket:
		case EPhysicalKey::RightBracket:
		case EPhysicalKey::Backslash:
		case EPhysicalKey::NonUsHash:
		case EPhysicalKey::Semicolon:
		case EPhysicalKey::Apostrophe:
		case EPhysicalKey::Grave:
		case EPhysicalKey::Comma:
		case EPhysicalKey::Period:
		case EPhysicalKey::Slash:
		case EPhysicalKey::CapsLock:
		case EPhysicalKey::F1:
		case EPhysicalKey::F2:
		case EPhysicalKey::F3:
		case EPhysicalKey::F4:
		case EPhysicalKey::F5:
		case EPhysicalKey::F6:
		case EPhysicalKey::F7:
		case EPhysicalKey::F8:
		case EPhysicalKey::F9:
		case EPhysicalKey::F10:
		case EPhysicalKey::F11:
		case EPhysicalKey::F12:
		case EPhysicalKey::PrintScreen:
		case EPhysicalKey::ScrollLock:
		case EPhysicalKey::Pause:
		case EPhysicalKey::Insert:
		case EPhysicalKey::Home:
		case EPhysicalKey::PageUp:
		case EPhysicalKey::Delete:
		case EPhysicalKey::End:
		case EPhysicalKey::PageDown:
		case EPhysicalKey::Right:
		case EPhysicalKey::Left:
		case EPhysicalKey::Down:
		case EPhysicalKey::Up:
		case EPhysicalKey::NumLock:
		case EPhysicalKey::KeypadDivide:
		case EPhysicalKey::KeypadMultiply:
		case EPhysicalKey::KeypadSubtract:
		case EPhysicalKey::KeypadAdd:
		case EPhysicalKey::KeypadEnter:
		case EPhysicalKey::Keypad1:
		case EPhysicalKey::Keypad2:
		case EPhysicalKey::Keypad3:
		case EPhysicalKey::Keypad4:
		case EPhysicalKey::Keypad5:
		case EPhysicalKey::Keypad6:
		case EPhysicalKey::Keypad7:
		case EPhysicalKey::Keypad8:
		case EPhysicalKey::Keypad9:
		case EPhysicalKey::Keypad0:
		case EPhysicalKey::KeypadDecimal:
		case EPhysicalKey::NonUsBackslash:
		case EPhysicalKey::Application:
		case EPhysicalKey::LeftControl:
		case EPhysicalKey::LeftShift:
		case EPhysicalKey::LeftAlt:
		case EPhysicalKey::LeftMeta:
		case EPhysicalKey::RightControl:
		case EPhysicalKey::RightShift:
		case EPhysicalKey::RightAlt:
		case EPhysicalKey::RightMeta:
			return true;
		case EPhysicalKey::Unknown:
		default:
			return false;
		}
	}

	[[nodiscard]] constexpr bool IsDefinedPointerButton(const EPointerButton button) noexcept
	{
		switch (button)
		{
		case EPointerButton::Left:
		case EPointerButton::Right:
		case EPointerButton::Middle:
		case EPointerButton::X1:
		case EPointerButton::X2:
			return true;
		default:
			return false;
		}
	}

	enum class EPointerRelativeSource : core::uint8
	{
		// RAWMOUSE without MOUSE_MOVE_ABSOLUTE: a signed device-count delta. No
		// acceleration curve, no screen clamp, no DPI factor.
		DeviceCounts,
		// RAWMOUSE with MOUSE_MOVE_ABSOLUTE, differenced: a delta over a normalized
		// 0 to 65535 space that a desktop boundary CAN clamp, whose scale is a
		// fraction of a desktop rather than a count of device ticks.
		NormalizedCoordinates
	};

	enum class EWindowEventKind : core::uint8
	{
		KeyChanged,
		TextInput,
		PointerButtonChanged,
		PointerMoved,
		PointerWheel,
		FocusChanged,
		DpiChanged,
		Resized,
		CloseRequested,
		PointerRelativeMoved,
		CompositionStart,
		CompositionUpdate,
		CompositionCommit,
		CompositionCancel,
		TextEdit,
		TextSelection,
		PointerLeft
	};

	struct WindowKeyEvent final
	{
		EPhysicalKey Key = EPhysicalKey::Unknown;

		bool bPressed = false;
		bool bRepeated = false;

		// Synthetic release after lost input authority; never a user commit gesture.
		bool bCancelled = false;

		// Physical transition remains observable; the native text service already handled its editing action.
		bool bTextServiceHandled = false;
	};

	struct WindowTextEvent final
	{
		core::uint32 CodePoint = 0;
	};

	struct WindowCompositionEvent final
	{
		static constexpr core::uint32 MAX_BYTES = 1024;

		char Utf8[MAX_BYTES]{};
		core::uint32 ByteCount = 0;
		core::uint32 CaretByte = 0;

		core::uint64 Session = 0;
		core::uint64 ExpectedRevision = 0;

		core::uint32 StartByte = 0;
		core::uint32 EraseBytes = 0;

		core::uint32 SelectionAnchorByte = 0;
		core::uint32 SelectionCaretByte = 0;

		bool bExplicitRange = false;
	};

	struct WindowTextInputState final
	{
		core::uint64 Session = 0;

		core::int32 CaretX = 0;
		core::int32 CaretY = 0;
		core::int32 CaretHeight = 0;

		bool bEnabled = false;
		bool bCancelComposition = false;
	};

	struct WindowPointerButtonEvent final
	{
		EPointerButton Button = EPointerButton::Left;
		core::int32 X = 0;
		core::int32 Y = 0;

		bool bPressed = false;
		bool bCancelled = false;
	};

	struct WindowPointerMoveEvent final
	{
		core::int32 X = 0;
		core::int32 Y = 0;

		core::int32 DeltaX = 0;
		core::int32 DeltaY = 0;
	};

	struct WindowPointerRelativeEvent final
	{

		core::int32 DeltaX = 0;
		core::int32 DeltaY = 0;

		// Units of the relative delta; device counts and pixels require different scaling.
		EPointerRelativeSource Source = EPointerRelativeSource::DeviceCounts;
	};

	struct WindowPointerWheelEvent final
	{

		core::float32 Horizontal = 0.0f;
		core::float32 Vertical = 0.0f;
		core::int32 X = 0;
		core::int32 Y = 0;

		// Client pixels when supplied; synthesized detents may omit a position.
		bool bHasPosition = false;
	};

	struct WindowFocusEvent final
	{
		bool bFocused = false;
	};

	struct WindowDpiEvent final
	{
		core::uint32 DpiX = 96;
		core::uint32 DpiY = 96;
	};

	struct WindowResizeEvent final
	{
		WindowExtent ClientExtent;
		bool bMinimized = false;
	};

	struct WindowEvent final
	{
		WindowId Window;
		EWindowEventKind Kind = EWindowEventKind::CloseRequested;

		WindowKeyEvent Key;
		WindowTextEvent Text;
		WindowCompositionEvent Composition;
		WindowPointerButtonEvent PointerButton;
		WindowPointerMoveEvent PointerMove;
		WindowPointerRelativeEvent PointerRelative;
		WindowPointerWheelEvent PointerWheel;
		WindowFocusEvent Focus;
		WindowDpiEvent Dpi;
		WindowResizeEvent Resize;
	};

	struct WindowEventBatch final
	{
		core::Span<const WindowEvent> Events;

		bool bOverflowed = false;
		bool bQuitRequested = false;
	};

	static_assert(sizeof(WindowId) == 16);
	static_assert(rox::core::IsStandardLayout<WindowId>&& rox::core::IsTriviallyCopyable<WindowId>);
	static_assert(rox::core::IsStandardLayout<WindowEvent>&& rox::core::IsTriviallyCopyable<WindowEvent>);
} // namespace rox::os
