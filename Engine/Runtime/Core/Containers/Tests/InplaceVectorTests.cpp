#include <InplaceVector.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(InplaceVectorTests, BoundedAdmission)
{
	InplaceVector<int32, 2> fixed;
	fixed.EmplaceRequired(3);
	fixed.EmplaceRequired(5);
	ASSERT_TRUE(fixed.Emplace(7) == nullptr);
	ASSERT_TRUE(fixed.Size() == 2 && fixed[0] == 3 && fixed[1] == 5);
	fixed.PopBack();
	ASSERT_TRUE(fixed.Emplace(7) != nullptr && fixed[1] == 7);
}
