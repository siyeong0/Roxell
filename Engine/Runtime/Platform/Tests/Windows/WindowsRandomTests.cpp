#include <WindowsRandom.h>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::os;

TEST(WindowsRandomTests, EmptyRequestSucceeds)
{
	EXPECT_TRUE(FillWindowsRandom({}));
}

TEST(WindowsRandomTests, FillingSubspanPreservesSurroundingBytes)
{
	Byte bytes[258]{};
	bytes[0] = static_cast<Byte>(0x5a);
	bytes[257] = static_cast<Byte>(0xa5);

	ASSERT_TRUE(FillWindowsRandom(Span<Byte>(bytes + 1, 256)));
	EXPECT_EQ(bytes[0], static_cast<Byte>(0x5a));
	EXPECT_EQ(bytes[257], static_cast<Byte>(0xa5));

	ASSERT_TRUE(FillWindowsRandom(Span<Byte>(bytes + 1, 1)));
	EXPECT_EQ(bytes[0], static_cast<Byte>(0x5a));
	EXPECT_EQ(bytes[257], static_cast<Byte>(0xa5));
}
