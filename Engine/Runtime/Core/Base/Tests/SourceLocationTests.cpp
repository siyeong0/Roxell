#include <SourceLocation.h>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(SourceLocationTests, CurrentCapturesCallSite)
{
	constexpr uint32 EXPECTED_LINE = __LINE__ + 1;
	const SourceLocation location = SourceLocation::current();
	EXPECT_EQ(location.line(), EXPECTED_LINE);
	EXPECT_STREQ(location.file_name(), __FILE__);
	EXPECT_NE(location.function_name()[0], '\0');
}
