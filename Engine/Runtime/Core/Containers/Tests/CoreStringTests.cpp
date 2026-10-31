#include <CoreString.hpp>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(CoreStringTests, CountedTextAndRejectedMutation)
{
	FailingMemoryResource resource(SystemMemoryResource::Get(), 1);
	String text(resource);
	const char original[] = {'a', '\0', 'b'};
	ASSERT_TRUE(text.Assign(StringView(original, sizeof(original))));
	ASSERT_EQ(text.Size(), sizeof(original));
	EXPECT_EQ(text.CStr()[1], '\0');
	EXPECT_EQ(text.CStr()[2], 'b');
	EXPECT_EQ(text.CStr()[3], '\0');
	resource.Reset(0);
	const char invalid[] = {'\xC0', '\x80'};
	EXPECT_FALSE(text.Assign(StringView(invalid, sizeof(invalid))));
	EXPECT_FALSE(text.Append(StringView("this text requires additional storage")));
	ASSERT_EQ(text.Size(), sizeof(original));
	EXPECT_EQ(text.View(), StringView(original, sizeof(original)));
	EXPECT_EQ(text.CStr()[3], '\0');
}
