#include <Vocabulary.hpp>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(VocabularyTests, OptionalAndVariantRetainTheirActiveValue)
{
	Optional<int32> value = NullOptional;
	EXPECT_FALSE(value.has_value());
	value = 7;
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(*value, 7);
	Variant<int32, float32> variant(InPlaceType<float32>, 2.5f);
	EXPECT_TRUE(HoldsAlternative<float32>(variant));
	ASSERT_NE(GetIf<float32>(&variant), nullptr);
	EXPECT_FLOAT_EQ(Get<float32>(variant), 2.5f);
	variant = int32{4};
	EXPECT_EQ(GetIf<float32>(&variant), nullptr);
	EXPECT_EQ(Get<int32>(variant), 4);
}

TEST(VocabularyTests, ApplyAndTieUseTupleValues)
{
	Tuple<int32, int32> values{3, 4};
	const auto add = [](const int32 left, const int32 right)
	{
		return left + right;
	};
	EXPECT_EQ(Apply(add, values), 7);
	int32 first = 0;
	int32 second = 0;
	Tie(first, second) = values;
	EXPECT_EQ(first, 3);
	EXPECT_EQ(second, 4);
}
