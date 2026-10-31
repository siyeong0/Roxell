#include <Hashing.hpp>
#include <Types.h>

#include <gtest/gtest.h>

#include <string_view>

using namespace rox::core;

TEST(HashingTests, HashBytesMatchesFnv1aVectors)
{
	constexpr uint8 BYTES[] = {0x00, 0xff, 0x80, 0x41};
	EXPECT_EQ(HashBytes(nullptr, 0), 0xcbf29ce484222325ull);
	EXPECT_EQ(HashBytes("hello", 5), 0xa430d84680aabd0bull);
	EXPECT_EQ(HashBytes(BYTES, sizeof(BYTES)), 0xd865f27bf6291553ull);
}

TEST(HashingTests, TextLookupsAndSignedZerosHashConsistently)
{
	const StringView text("hello");
	const std::string_view standardText("hello");
	EXPECT_EQ(DefaultHash<StringView>{}(text), 0xa430d84680aabd0bull);
	EXPECT_EQ(DefaultHash<StringView>{}(standardText), 0xa430d84680aabd0bull);
	EXPECT_EQ(DefaultHash<std::string_view>{}(standardText), 0xa430d84680aabd0bull);
	EXPECT_TRUE(DefaultEqual<StringView>{}(text, standardText));
	EXPECT_EQ(DefaultHash<float32>{}(0.0f), DefaultHash<float32>{}(-0.0f));
	EXPECT_EQ(DefaultHash<float64>{}(0.0), DefaultHash<float64>{}(-0.0));
}
