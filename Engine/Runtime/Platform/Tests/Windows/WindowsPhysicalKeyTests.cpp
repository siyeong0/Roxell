#include "../../Private/Windows/WindowsPhysicalKeyTable.h"
#include <gtest/gtest.h>

namespace
{
	using rox::os::EPhysicalKey;
	using rox::os::PhysicalKeyFromScanCode;

	TEST(WindowsPhysicalKeyTests, MapsVirtualKeysWhenScanCodeIsAbsent)
	{
		EXPECT_EQ(PhysicalKeyFromScanCode(VK_F9, 0), EPhysicalKey::F9);
		EXPECT_EQ(PhysicalKeyFromScanCode(VK_ESCAPE, 0), EPhysicalKey::Escape);
		EXPECT_EQ(PhysicalKeyFromScanCode(VK_SPACE, 0), EPhysicalKey::Space);
		EXPECT_EQ(PhysicalKeyFromScanCode(VK_RCONTROL, 0), EPhysicalKey::RightControl);
		EXPECT_EQ(PhysicalKeyFromScanCode(VK_LEFT, 0), EPhysicalKey::Left);
		EXPECT_EQ(PhysicalKeyFromScanCode(VK_PAUSE, 0), EPhysicalKey::Pause);
	}

	TEST(WindowsPhysicalKeyTests, ScanCodeTakesPrecedenceOverVirtualKey)
	{
		EXPECT_EQ(PhysicalKeyFromScanCode(VK_F9, 0x001E0000), EPhysicalKey::A);
		EXPECT_EQ(PhysicalKeyFromScanCode(VK_LEFT, 0x004B0000), EPhysicalKey::Keypad4);
		EXPECT_EQ(PhysicalKeyFromScanCode(VK_LEFT, 0x014B0000), EPhysicalKey::Left);
	}

	TEST(WindowsPhysicalKeyTests, UnknownCodesDoNotMapToKeys)
	{
		EXPECT_EQ(PhysicalKeyFromScanCode(0, 0), EPhysicalKey::Unknown);
		EXPECT_EQ(PhysicalKeyFromScanCode(0x100, 0), EPhysicalKey::Unknown);
	}
}
