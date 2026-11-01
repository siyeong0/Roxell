#include "WindowsPhysicalKeyTable.h"

namespace rox::os
{
	namespace
	{
		constexpr core::uint32 WINDOWS_EXTENDED_SCAN_CODE_FLAG = 0x100;
	} // namespace

	EPhysicalKey PhysicalKeyFromScanCode(const WPARAM virtualKey, const LPARAM keyData) noexcept
	{
		if (virtualKey == VK_PAUSE)
		{
			return EPhysicalKey::Pause;
		}

		core::uint32 scanCode = static_cast<core::uint32>((keyData >> 16) & 0xFF);
		bool bExtended = (keyData & 0x01000000) != 0;

		if (scanCode == 0 && virtualKey <= 0xFF)
		{
			// Accessibility input may provide only a virtual key. Native scan codes remain authoritative.
			switch (virtualKey)
			{
			case VK_LCONTROL:
				return EPhysicalKey::LeftControl;
			case VK_RCONTROL:
				return EPhysicalKey::RightControl;
			case VK_LSHIFT:
				return EPhysicalKey::LeftShift;
			case VK_RSHIFT:
				return EPhysicalKey::RightShift;
			case VK_LMENU:
				return EPhysicalKey::LeftAlt;
			case VK_RMENU:
				return EPhysicalKey::RightAlt;
			case VK_LEFT:
				return EPhysicalKey::Left;
			case VK_RIGHT:
				return EPhysicalKey::Right;
			case VK_UP:
				return EPhysicalKey::Up;
			case VK_DOWN:
				return EPhysicalKey::Down;
			case VK_HOME:
				return EPhysicalKey::Home;
			case VK_END:
				return EPhysicalKey::End;
			case VK_PRIOR:
				return EPhysicalKey::PageUp;
			case VK_NEXT:
				return EPhysicalKey::PageDown;
			case VK_INSERT:
				return EPhysicalKey::Insert;
			case VK_DELETE:
				return EPhysicalKey::Delete;
			default:
				break;
			}

			const UINT mapped = MapVirtualKeyExW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(0));
			scanCode = mapped & 0xFF;
			bExtended = bExtended || (mapped & 0xFF00) == 0xE000;
		}

		const core::uint32 key = scanCode | (bExtended ? WINDOWS_EXTENDED_SCAN_CODE_FLAG : 0);

		switch (key)
		{
		case 0x001:
			return EPhysicalKey::Escape;
		case 0x002:
			return EPhysicalKey::Digit1;
		case 0x003:
			return EPhysicalKey::Digit2;
		case 0x004:
			return EPhysicalKey::Digit3;
		case 0x005:
			return EPhysicalKey::Digit4;
		case 0x006:
			return EPhysicalKey::Digit5;
		case 0x007:
			return EPhysicalKey::Digit6;
		case 0x008:
			return EPhysicalKey::Digit7;
		case 0x009:
			return EPhysicalKey::Digit8;
		case 0x00A:
			return EPhysicalKey::Digit9;
		case 0x00B:
			return EPhysicalKey::Digit0;
		case 0x00C:
			return EPhysicalKey::Minus;
		case 0x00D:
			return EPhysicalKey::Equal;
		case 0x00E:
			return EPhysicalKey::Backspace;
		case 0x00F:
			return EPhysicalKey::Tab;
		case 0x010:
			return EPhysicalKey::Q;
		case 0x011:
			return EPhysicalKey::W;
		case 0x012:
			return EPhysicalKey::E;
		case 0x013:
			return EPhysicalKey::R;
		case 0x014:
			return EPhysicalKey::T;
		case 0x015:
			return EPhysicalKey::Y;
		case 0x016:
			return EPhysicalKey::U;
		case 0x017:
			return EPhysicalKey::I;
		case 0x018:
			return EPhysicalKey::O;
		case 0x019:
			return EPhysicalKey::P;
		case 0x01A:
			return EPhysicalKey::LeftBracket;
		case 0x01B:
			return EPhysicalKey::RightBracket;
		case 0x01C:
			return EPhysicalKey::Enter;
		case 0x01D:
			return EPhysicalKey::LeftControl;
		case 0x01E:
			return EPhysicalKey::A;
		case 0x01F:
			return EPhysicalKey::S;
		case 0x020:
			return EPhysicalKey::D;
		case 0x021:
			return EPhysicalKey::F;
		case 0x022:
			return EPhysicalKey::G;
		case 0x023:
			return EPhysicalKey::H;
		case 0x024:
			return EPhysicalKey::J;
		case 0x025:
			return EPhysicalKey::K;
		case 0x026:
			return EPhysicalKey::L;
		case 0x027:
			return EPhysicalKey::Semicolon;
		case 0x028:
			return EPhysicalKey::Apostrophe;
		case 0x029:
			return EPhysicalKey::Grave;
		case 0x02A:
			return EPhysicalKey::LeftShift;
		case 0x02B:
			return EPhysicalKey::Backslash;
		case 0x02C:
			return EPhysicalKey::Z;
		case 0x02D:
			return EPhysicalKey::X;
		case 0x02E:
			return EPhysicalKey::C;
		case 0x02F:
			return EPhysicalKey::V;
		case 0x030:
			return EPhysicalKey::B;
		case 0x031:
			return EPhysicalKey::N;
		case 0x032:
			return EPhysicalKey::M;
		case 0x033:
			return EPhysicalKey::Comma;
		case 0x034:
			return EPhysicalKey::Period;
		case 0x035:
			return EPhysicalKey::Slash;
		case 0x036:
			return EPhysicalKey::RightShift;
		case 0x037:
			return EPhysicalKey::KeypadMultiply;
		case 0x038:
			return EPhysicalKey::LeftAlt;
		case 0x039:
			return EPhysicalKey::Space;
		case 0x03A:
			return EPhysicalKey::CapsLock;
		case 0x03B:
			return EPhysicalKey::F1;
		case 0x03C:
			return EPhysicalKey::F2;
		case 0x03D:
			return EPhysicalKey::F3;
		case 0x03E:
			return EPhysicalKey::F4;
		case 0x03F:
			return EPhysicalKey::F5;
		case 0x040:
			return EPhysicalKey::F6;
		case 0x041:
			return EPhysicalKey::F7;
		case 0x042:
			return EPhysicalKey::F8;
		case 0x043:
			return EPhysicalKey::F9;
		case 0x044:
			return EPhysicalKey::F10;
		case 0x045:
			return EPhysicalKey::NumLock;
		case 0x046:
			return EPhysicalKey::ScrollLock;
		case 0x047:
			return EPhysicalKey::Keypad7;
		case 0x048:
			return EPhysicalKey::Keypad8;
		case 0x049:
			return EPhysicalKey::Keypad9;
		case 0x04A:
			return EPhysicalKey::KeypadSubtract;
		case 0x04B:
			return EPhysicalKey::Keypad4;
		case 0x04C:
			return EPhysicalKey::Keypad5;
		case 0x04D:
			return EPhysicalKey::Keypad6;
		case 0x04E:
			return EPhysicalKey::KeypadAdd;
		case 0x04F:
			return EPhysicalKey::Keypad1;
		case 0x050:
			return EPhysicalKey::Keypad2;
		case 0x051:
			return EPhysicalKey::Keypad3;
		case 0x052:
			return EPhysicalKey::Keypad0;
		case 0x053:
			return EPhysicalKey::KeypadDecimal;
		case 0x056:
			return EPhysicalKey::NonUsBackslash;
		case 0x057:
			return EPhysicalKey::F11;
		case 0x058:
			return EPhysicalKey::F12;
		case 0x11C:
			return EPhysicalKey::KeypadEnter;
		case 0x11D:
			return EPhysicalKey::RightControl;
		case 0x135:
			return EPhysicalKey::KeypadDivide;
		case 0x137:
			return EPhysicalKey::PrintScreen;
		case 0x138:
			return EPhysicalKey::RightAlt;
		case 0x147:
			return EPhysicalKey::Home;
		case 0x148:
			return EPhysicalKey::Up;
		case 0x149:
			return EPhysicalKey::PageUp;
		case 0x14B:
			return EPhysicalKey::Left;
		case 0x14D:
			return EPhysicalKey::Right;
		case 0x14F:
			return EPhysicalKey::End;
		case 0x150:
			return EPhysicalKey::Down;
		case 0x151:
			return EPhysicalKey::PageDown;
		case 0x152:
			return EPhysicalKey::Insert;
		case 0x153:
			return EPhysicalKey::Delete;
		case 0x15B:
			return EPhysicalKey::LeftMeta;
		case 0x15C:
			return EPhysicalKey::RightMeta;
		case 0x15D:
			return EPhysicalKey::Application;
		default:
			return EPhysicalKey::Unknown;
		}
	}
} // namespace rox::os
