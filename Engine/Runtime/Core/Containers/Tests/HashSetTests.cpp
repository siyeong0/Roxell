#include <FailingMemoryResource.h>
#include <HashSet.hpp>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(HashSetTests, DuplicateAndFailureKeepExistingKeys)
{
	FailingMemoryResource resource(SystemMemoryResource::Get(), 0);
	HashSet<int32> values(resource);
	const HashSet<int32>::InsertResult refused = values.Insert(7);
	EXPECT_FALSE(refused.bSucceeded);
	EXPECT_TRUE(values.IsEmpty());
	resource.Reset(1);
	ASSERT_TRUE(values.Reserve(2));
	ASSERT_TRUE(values.Insert(7).bInserted);
	ASSERT_TRUE(values.Insert(9).bInserted);
	resource.Reset(0);
	const HashSet<int32>::InsertResult duplicate = values.Insert(7);
	EXPECT_TRUE(duplicate.bSucceeded);
	EXPECT_FALSE(duplicate.bInserted);
	EXPECT_EQ(values.Size(), 2u);
	EXPECT_TRUE(values.Erase(7));
	EXPECT_FALSE(values.Contains(7));
	EXPECT_TRUE(values.Contains(9));
}
