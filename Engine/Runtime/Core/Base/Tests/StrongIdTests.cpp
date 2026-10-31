#include <StrongId.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	struct TestIdTag;
	using TestId = StrongId<TestIdTag>;
}

TEST(StrongIdTests, StrongIdTextAndFailedParse)
{
	TestId id;
	ASSERT_TRUE(TryParseStrongId("{01234567-89AB-CDEF-FEDC-BA9876543210}", &id));
	ASSERT_EQ(id.High, 0x0123456789abcdefULL);
	ASSERT_EQ(id.Low, 0xfedcba9876543210ULL);
	ASSERT_EQ(ToString(id), "0123456789abcdeffedcba9876543210");
	const TestId before = id;
	for (const StringView invalid : {StringView{}, StringView("0123456789abcdeffedcba987654321g"),
	                                 StringView("0123456789abcdeffedcba987654321")})
	{
		ASSERT_FALSE(TryParseStrongId(invalid, &id));
		ASSERT_EQ(id, before);
	}
	ASSERT_TRUE(TryParseStrongId("00000000000000000000000000000000", &id));
	ASSERT_FALSE(id.IsValid());
}
