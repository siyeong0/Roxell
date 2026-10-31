#include <FunctionRef.hpp>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(FunctionRefTests, CopiesBorrowTheSameMutableTarget)
{
	int32 total = 0;
	auto add = [&total](const int32 value) noexcept
	{
		total += value;
		return total;
	};
	FunctionRef<int32(int32) noexcept> first(add);
	const FunctionRef<int32(int32) noexcept> copy(first);
	EXPECT_EQ(first(2), 2);
	EXPECT_EQ(copy(3), 5);
	EXPECT_EQ(total, 5);
}
