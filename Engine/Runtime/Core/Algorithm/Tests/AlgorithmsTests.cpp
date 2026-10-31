#include <Algorithms.hpp>
#include <Types.h>

#include <gtest/gtest.h>

#include <array>
#include <functional>

using namespace rox::core;

TEST(AlgorithmsTests, SortingAndSearchRespectComparator)
{
	std::array<int32, 6> values{3, -1, 4, 3, 0, 2};
	Sort(values.begin(), values.end(), std::greater<int32>{});
	ASSERT_EQ(values, (std::array<int32, 6>{4, 3, 3, 2, 0, -1}));
	ASSERT_TRUE(IsSorted(values.begin(), values.end(), std::greater<int32>{}));
	ASSERT_TRUE(!IsSorted(values.begin(), values.end()));
	ASSERT_EQ(LowerBound(values.begin(), values.end(), 3, std::greater<int32>{}), values.begin() + 1);
	ASSERT_EQ(UpperBound(values.begin(), values.end(), 3, std::greater<int32>{}), values.begin() + 3);
	ASSERT_TRUE(BinarySearch(values.begin(), values.end(), 2, std::greater<int32>{}));
	ASSERT_TRUE(!BinarySearch(values.begin(), values.end(), 5, std::greater<int32>{}));
	ASSERT_EQ(LowerBound(values.begin(), values.begin(), 3), values.begin());
}

TEST(AlgorithmsTests, CopyingFillingAndSearchingRanges)
{
	const std::array<int32, 4> source{7, 2, 7, 4};
	std::array<int32, 6> output{-1, -1, -1, -1, -1, -1};
	ASSERT_EQ(Copy(source.begin(), source.end(), output.begin() + 1), output.begin() + 5);
	ASSERT_EQ(output, (std::array<int32, 6>{-1, 7, 2, 7, 4, -1}));
	ASSERT_EQ(CopyN(source.begin(), 0, output.begin()), output.begin());
	ASSERT_EQ(CopyN(source.begin(), 2, output.begin()), output.begin() + 2);
	Fill(output.begin() + 2, output.begin() + 4, 9);
	ASSERT_EQ(FillN(output.begin() + 4, 2, 8), output.end());
	ASSERT_EQ(output, (std::array<int32, 6>{7, 2, 9, 9, 8, 8}));
	ASSERT_EQ(Find(output.begin(), output.end(), 9), output.begin() + 2);
	ASSERT_EQ(Find(output.begin(), output.end(), 42), output.end());
	ASSERT_TRUE(FindIf(output.begin(), output.end(), [](const int32 value)
	{
		return value < 5;
	}) == output.begin() + 1);
	ASSERT_EQ(AdjacentFind(output.begin(), output.end()), output.begin() + 2);
}

TEST(AlgorithmsTests, PredicatesAndComparisonHandleEmptyRanges)
{
	const std::array<int32, 3> values{1, 2, 3};
	const std::array<int32, 3> same{1, 2, 3};
	const std::array<int32, 3> later{1, 2, 4};
	const auto positive = [](const int32 value)
	{
		return value > 0;
	};
	ASSERT_TRUE(Equal(values.begin(), values.end(), same.begin(), same.end()));
	ASSERT_TRUE(!Equal(values.begin(), values.end(), later.begin(), later.end()));
	ASSERT_TRUE(LexicographicalCompare(values.begin(), values.end(), later.begin(), later.end()));
	ASSERT_TRUE(!LexicographicalCompare(later.begin(), later.end(), values.begin(), values.end()));
	ASSERT_TRUE(AllOf(values.begin(), values.end(), positive));
	ASSERT_TRUE(AnyOf(values.begin(), values.end(), positive));
	ASSERT_TRUE(!NoneOf(values.begin(), values.end(), positive));
	ASSERT_TRUE(AllOf(values.begin(), values.begin(), positive));
	ASSERT_TRUE(!AnyOf(values.begin(), values.begin(), positive));
	ASSERT_TRUE(NoneOf(values.begin(), values.begin(), positive));
}

TEST(AlgorithmsTests, RearrangementReturnsExpectedPositions)
{
	std::array<int32, 5> values{1, 2, 3, 4, 5};
	ASSERT_EQ(Rotate(values.begin(), values.begin() + 2, values.end()), values.begin() + 3);
	ASSERT_EQ(values, (std::array<int32, 5>{3, 4, 5, 1, 2}));
	Reverse(values.begin(), values.end());
	Replace(values.begin(), values.end(), 4, 8);
	ASSERT_EQ(values, (std::array<int32, 5>{2, 1, 5, 8, 3}));
	NthElement(values.begin(), values.begin() + 2, values.end());
	ASSERT_EQ(values[2], 3);
	ASSERT_TRUE(values[0] <= 3);
	ASSERT_TRUE(values[1] <= 3);
	ASSERT_TRUE(values[3] >= 3);
	ASSERT_TRUE(values[4] >= 3);
	ASSERT_EQ(Begin(values), values.begin());
	ASSERT_EQ(End(values), values.end());
	ASSERT_EQ(Size(values), 5);
}

TEST(AlgorithmsTests, HeapOperationsHonorMinimumComparator)
{
	std::array<int32, 5> values{4, 1, 3, 2, 0};
	const std::greater<int32> greater{};
	MakeHeap(values.begin(), values.begin() + 4, greater);
	ASSERT_TRUE(IsHeap(values.begin(), values.begin() + 4, greater));
	ASSERT_EQ(values[0], 1);
	PushHeap(values.begin(), values.end(), greater);
	ASSERT_EQ(values[0], 0);
	ASSERT_TRUE(IsHeap(values.begin(), values.end(), greater));
	PopHeap(values.begin(), values.end(), greater);
	ASSERT_EQ(values[4], 0);
	ASSERT_EQ(values[0], 1);
	ASSERT_TRUE(IsHeap(values.begin(), values.begin() + 4, greater));
}
