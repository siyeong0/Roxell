#include <StringView.hpp>
#include <Utf8.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(Utf8Tests, RejectsTruncatedOverlongAndSurrogateSequences)
{
	EXPECT_TRUE(IsValidUtf8(StringView("A\xEA\xB0\x80")));
	EXPECT_TRUE(IsValidUtf8(StringView{}));
	const char truncated[] = {'\xE2', '\x82'};
	const char overlong[] = {'\xC0', '\x80'};
	const char surrogate[] = {'\xED', '\xA0', '\x80'};
	EXPECT_FALSE(IsValidUtf8(StringView(truncated, sizeof(truncated))));
	EXPECT_FALSE(IsValidUtf8(StringView(overlong, sizeof(overlong))));
	EXPECT_FALSE(IsValidUtf8(StringView(surrogate, sizeof(surrogate))));
}
