#include <TextConversion.hpp>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(TextConversionTests, ParseReturnsEndOfConsumedPrefix)
{
	const char text[] = "-12rest";
	char* end = nullptr;
	const float64 value = ParseDouble(text, &end);
	EXPECT_DOUBLE_EQ(value, -12.0);
	EXPECT_EQ(end, text + 3);
	EXPECT_EQ(ToLower('A'), 'a');
}
