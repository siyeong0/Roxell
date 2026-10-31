#include <ByteOrder.h>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(ByteOrderTests, NativeOrderMatchesObjectRepresentation)
{
	const uint32 value = 0x01020304;
	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&value);
	if constexpr (ByteOrder::native == ByteOrder::little)
	{
		EXPECT_EQ(bytes[0], 0x04);
		EXPECT_EQ(bytes[3], 0x01);
	}
	else if constexpr (ByteOrder::native == ByteOrder::big)
	{
		EXPECT_EQ(bytes[0], 0x01);
		EXPECT_EQ(bytes[3], 0x04);
	}
	else
	{
		GTEST_SKIP() << "Mixed byte order is not covered by this test.";
	}
}
