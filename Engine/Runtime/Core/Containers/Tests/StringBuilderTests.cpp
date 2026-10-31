#include <FailingMemoryResource.h>
#include <StringBuilder.hpp>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(StringBuilderTests, FailedAppendPreservesTextAndTerminator)
{
	FailingMemoryResource resource(SystemMemoryResource::Get(), 1);
	StringBuilder builder(resource);
	ASSERT_TRUE(builder.Reserve(4));
	ASSERT_TRUE(builder.Append(StringView("ab")));
	ASSERT_TRUE(builder.AppendInteger(int32{-3}));
	const usize capacity = builder.Capacity();
	while (builder.Size() < capacity)
	{
		ASSERT_TRUE(builder.Append('x'));
	}
	resource.Reset(0);
	EXPECT_FALSE(builder.Append('y'));
	const char invalid[] = {'\xC0', '\x80'};
	EXPECT_FALSE(builder.Append(StringView(invalid, sizeof(invalid))));
	ASSERT_EQ(builder.Size(), capacity);
	EXPECT_EQ(builder.View().Substr(0, 4), StringView("ab-3"));
	for (usize index = 4; index < capacity; ++index)
	{
		EXPECT_EQ(builder.View()[index], 'x');
	}
	EXPECT_EQ(builder.CStr()[capacity], '\0');
	builder.Clear();
	EXPECT_TRUE(builder.IsEmpty());
	EXPECT_EQ(builder.CStr()[0], '\0');
}
