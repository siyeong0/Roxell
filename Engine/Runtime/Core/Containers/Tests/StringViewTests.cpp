#include <StringView.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(StringViewTests, CountedSearchIncludesEmbeddedNull)
{
	const char bytes[] = {'a', '\0', 'b', 'a'};
	const StringView text(bytes, sizeof(bytes));
	EXPECT_EQ(text.Size(), 4u);
	EXPECT_EQ(text.Find('\0'), 1u);
	EXPECT_EQ(text.Find('a', 1), 3u);
	EXPECT_EQ(text.Find('z'), StringView::NPOS);
	EXPECT_EQ(text.Substr(1, 2), StringView(bytes + 1, 2));
	EXPECT_TRUE(text.StartsWith(StringView(bytes, 2)));
	EXPECT_TRUE(text.EndsWith(StringView("ba")));
	EXPECT_TRUE(text.Substr(text.Size()).IsEmpty());
}

TEST(StringViewTests, EmptyAndBorrowedRanges)
{
	StringView empty;
	EXPECT_EQ(empty.Data(), nullptr);
	EXPECT_EQ(empty.begin(), empty.end());
	EXPECT_EQ(empty.Substr(0).Data(), nullptr);
	EXPECT_TRUE(empty.StartsWith(StringView()));
	EXPECT_TRUE(empty.EndsWith(StringView()));
	EXPECT_EQ(empty.Find(StringView()), 0u);
	EXPECT_EQ(empty.Find('x'), StringView::NPOS);
	const std::string_view standard = empty;
	EXPECT_TRUE(standard.empty());

	char bytes[] = {'a', 'b', 'c', 'd'};
	StringView view(bytes, sizeof(bytes));
	view.remove_prefix(1);
	view.remove_suffix(1);
	EXPECT_EQ(view.Data(), bytes + 1);
	EXPECT_EQ(view.Size(), 2u);
	bytes[1] = 'x';
	EXPECT_EQ(view.front(), 'x');
	EXPECT_EQ(view.back(), 'c');
	EXPECT_EQ(view.Substr(1, 100), StringView("c"));
}

TEST(StringViewTests, SearchAndComparisonBoundaries)
{
	const StringView text("abacaba");
	EXPECT_EQ(text.Find(StringView("aba"), 1), 4u);
	EXPECT_EQ(text.Find(StringView(), text.Size()), text.Size());
	EXPECT_EQ(text.Find(StringView(), text.Size() + 1), StringView::NPOS);
	EXPECT_EQ(text.rfind(StringView("aba")), 4u);
	EXPECT_EQ(text.rfind('a', 3), 2u);
	EXPECT_EQ(text.find_first_of(StringView("cb")), 1u);
	EXPECT_EQ(text.find_first_not_of('a'), 1u);
	EXPECT_EQ(text.find_last_of('b'), 5u);
	EXPECT_EQ(text.find_last_not_of(StringView("ab")), 3u);
	EXPECT_TRUE(text.Contains('c'));
	EXPECT_FALSE(text.Contains(StringView("xyz")));
	EXPECT_FALSE(text.StartsWith(StringView("abacabad")));
	EXPECT_FALSE(text.EndsWith(StringView("abacabad")));
	EXPECT_LT(StringView("abc"), StringView("abd"));
	EXPECT_EQ(StringView("abc"), std::string_view("abc"));
}
