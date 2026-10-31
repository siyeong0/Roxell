#include <FailingMemoryResource.h>
#include <FlatSet.hpp>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(FlatSetTests, OrderedKeysAndAllocationFailure)
{
	FailingMemoryResource resource(SystemMemoryResource::Get(), 1);
	FlatSet<int32> values(resource);
	ASSERT_TRUE(values.Reserve(3));
	ASSERT_TRUE(values.Insert(30).bInserted);
	ASSERT_TRUE(values.Insert(10).bInserted);
	ASSERT_TRUE(values.Insert(20).bInserted);
	resource.Reset(0);
	const FlatSet<int32>::InsertResult duplicate = values.Insert(20);
	EXPECT_TRUE(duplicate.bSucceeded);
	EXPECT_FALSE(duplicate.bInserted);
	EXPECT_FALSE(values.Insert(40).bSucceeded);
	ASSERT_EQ(values.Size(), 3u);
	EXPECT_EQ(values.At(0), 10);
	EXPECT_EQ(values.At(1), 20);
	EXPECT_EQ(values.At(2), 30);
	ASSERT_NE(values.LowerBound(15), nullptr);
	EXPECT_EQ(*values.LowerBound(15), 20);
	EXPECT_TRUE(values.Erase(20));
	EXPECT_FALSE(values.Contains(20));
}
