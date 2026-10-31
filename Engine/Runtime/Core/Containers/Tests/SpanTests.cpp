#include <Span.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(SpanTests, SubrangesBorrowAndMutateOriginalStorage)
{
	int32 values[] = {1, 2, 3, 4};
	Span<int32> span(values);
	Span<int32> middle = span.Subspan(1, 2);
	ASSERT_EQ(middle.Size(), 2u);
	middle[0] = 7;
	EXPECT_EQ(values[1], 7);
	EXPECT_EQ(span.First(2).Back(), 7);
	EXPECT_EQ(span.Last(2).Front(), 3);
	EXPECT_EQ(span.SizeBytes(), sizeof(values));
	EXPECT_TRUE(span.Subspan(4).IsEmpty());
	EXPECT_EQ(Span<int32>{}.Subspan(0).Data(), nullptr);
}
