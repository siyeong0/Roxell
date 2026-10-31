#include <HashFunction.hpp>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(HashFunctionTests, EqualValuesProduceEqualHashes)
{
	const HashFunction<int32> hash{};
	const int32 first = 42;
	const int32 equal = 42;
	EXPECT_EQ(hash(first), hash(equal));
	EXPECT_EQ(hash(first), hash(first));
}
