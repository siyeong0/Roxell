#include <Name.hpp>
#include <NamePool.h>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(NameTests, EmptyAndLexicalComparisonAcrossPools)
{
	const Name empty;
	EXPECT_TRUE(empty.IsEmpty());
	EXPECT_TRUE(empty.View().IsEmpty());
	EXPECT_EQ(empty.GetHash(), 0u);
	EXPECT_EQ(DefaultHash<Name>{}(empty), 0u);

	NamePool first(SystemMemoryResource::Get());
	NamePool second(SystemMemoryResource::Get());
	const Name apple = first.Intern("apple");
	const Name same = second.Intern("apple");
	const Name prefix = first.Intern("app");
	const Name later = first.Intern("banana");
	EXPECT_EQ(apple, same);
	EXPECT_EQ(DefaultHash<Name>{}(apple), same.GetHash());
	EXPECT_NE(apple, prefix);
	EXPECT_LT(prefix, apple);
	EXPECT_LT(apple, later);
	EXPECT_GT(later, prefix);
	EXPECT_LT(empty, apple);
}
