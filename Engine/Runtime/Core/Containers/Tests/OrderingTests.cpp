#include <Ordering.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(OrderingTests, TextOrderingAcceptsBorrowedAndOwnedKeys)
{
	const String owned("alpha");
	const StringView later("beta");
	const DefaultLess<String> less;
	EXPECT_TRUE(less(owned, later));
	EXPECT_FALSE(less(later, owned));
	EXPECT_FALSE(less(owned, StringView("alpha")));
	EXPECT_TRUE(DefaultLess<int32>{}(1, 2));
}
