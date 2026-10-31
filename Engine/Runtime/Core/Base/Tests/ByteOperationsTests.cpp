#include <ByteOperations.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(ByteOperationsTests, CopiesMovesAndSearchesBytes)
{
	const char source[] = "abcde";
	char output[6]{};
	EXPECT_EQ(CopyBytes(output, source, sizeof(source)), output);
	EXPECT_EQ(CompareBytes(output, source, sizeof(source)), 0);
	EXPECT_EQ(FindByte(output, 'c', 5), output + 2);
	EXPECT_EQ(FindByte(output, 'z', 5), nullptr);
	EXPECT_EQ(MoveBytes(output + 1, output, 4), output + 1);
	EXPECT_EQ(CompareBytes(output, "aabcd", 5), 0);
	EXPECT_EQ(SetBytes(output + 1, 'x', 2), output + 1);
	EXPECT_EQ(CompareBytes(output, "axxcd", 5), 0);
}
