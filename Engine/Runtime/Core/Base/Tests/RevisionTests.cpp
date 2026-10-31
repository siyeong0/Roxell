#include <Revision.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(RevisionTests, DefaultsToZeroAndStoresValue)
{
	const Revision initial{};
	const Revision revision{42};
	EXPECT_EQ(initial.Value, 0);
	EXPECT_EQ(revision.Value, 42);
}
