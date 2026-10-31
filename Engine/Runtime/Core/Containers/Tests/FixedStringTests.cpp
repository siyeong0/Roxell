#include <FixedString.hpp>
#include <Utf8.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(FixedStringTests, ValidationAndCapacity)
{
	FixedString<8> text("ab");
	ASSERT_TRUE(text.Append("\xEA\xB0\x80"));
	const char invalid[] = {'\xC0', '\x80'};
	ASSERT_TRUE(!IsValidUtf8(StringView(invalid, sizeof(invalid))));
	ASSERT_TRUE(!text.Append(StringView(invalid, sizeof(invalid))));
	ASSERT_TRUE(!text.Append("1234"));
	ASSERT_TRUE(text.View() == StringView("ab\xEA\xB0\x80") && text.CStr()[5] == '\0');
	ASSERT_TRUE(text.Assign(text.View().Substr(2)));
	ASSERT_TRUE(text.View() == StringView("\xEA\xB0\x80"));
	FixedString<0> empty;
	ASSERT_TRUE(!empty.Append("x") && empty.Append(StringView{}));
}
