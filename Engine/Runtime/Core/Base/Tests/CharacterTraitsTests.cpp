#include <CharacterTraits.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(CharacterTraitsTests, ComparesCopiesAndFindsCharacters)
{
	using Traits = CharacterTraits<char>;
	char output[5]{};
	EXPECT_EQ(Traits::length("abcd"), 4);
	EXPECT_EQ(Traits::copy(output, "abcd", 4), output);
	EXPECT_EQ(Traits::compare(output, "abcd", 4), 0);
	EXPECT_LT(Traits::compare("abc", "abd", 3), 0);
	EXPECT_EQ(Traits::find(output, 4, 'c'), output + 2);
	EXPECT_EQ(Traits::find(output, 4, 'z'), nullptr);
	EXPECT_NE(Traits::eof(), Traits::to_int_type('a'));
}
