#include <Utility.hpp>
#include <Types.h>
#include <TypeTraits.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

static_assert(IsSame<decltype(Move(*static_cast<int32*>(nullptr))), int32&&>);
static_assert(IsSame<decltype(Forward<int32&>(*static_cast<int32*>(nullptr))), int32&>);
static_assert(IsSame<decltype(AsConst(*static_cast<int32*>(nullptr))), const int32&>);

TEST(UtilityTests, ExchangesSwapsAndInvokes)
{
	int32 first = 3;
	int32 second = 7;
	EXPECT_EQ(Exchange(first, 5), 3);
	EXPECT_EQ(first, 5);
	Swap(first, second);
	EXPECT_EQ(first, 7);
	EXPECT_EQ(second, 5);
	EXPECT_EQ(AddressOf(first), &first);
	EXPECT_EQ(ConstReference(first).get(), 7);
	const auto add = [](const int32 left, const int32 right)
	{
		return left + right;
	};
	EXPECT_EQ(Invoke(add, first, second), 12);
}

TEST(UtilityTests, BitOperationsHandleZeroAndSetBits)
{
	EXPECT_EQ(ToInteger<uint32>(Byte{0x81}), 0x81u);
	EXPECT_EQ(BitCast<uint32>(int32{-1}), 0xffffffffu);
	EXPECT_EQ(RotateRight(uint32{1}, 1), 0x80000000u);
	EXPECT_EQ(BitWidth(uint32{0}), 0);
	EXPECT_EQ(BitWidth(uint32{8}), 4);
	EXPECT_EQ(CountTrailingZeros(uint32{0}), 32);
	EXPECT_EQ(CountTrailingZeros(uint32{8}), 3);
	EXPECT_EQ(PopCount(uint32{0xf0}), 4);
}
