#include <Hash.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(HashTests, HashParsingRejectsMalformedTextWithoutMutation)
{
	Hash256 hash;
	ASSERT_TRUE(TryParseHash256("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F", &hash));
	ASSERT_EQ(ToString(hash), "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
	const Hash256 before = hash;
	ASSERT_FALSE(TryParseHash256("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1g", &hash));
	ASSERT_EQ(hash, before);
	ASSERT_FALSE(TryParseHash256("00", &hash));
	ASSERT_EQ(hash, before);
}
