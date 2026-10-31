#include <Int2.hpp>

#include <gtest/gtest.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

using namespace rox::math;

namespace
{
	void suppressAssertionDialog() noexcept
	{
#if defined(_WIN32)
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
	}
}

TEST(Int2Tests, ArithmeticAndConversionsRespectSignedLanes)
{
	const Int2 value{1, -2};
	EXPECT_EQ(value * 2, Int2(2, -4));
	EXPECT_EQ((value * 2) / 2, value);
	const Vector2f input{1.5f, -1.5f};
	EXPECT_EQ(Int2::Floor(input), Int2(1, -2));
	EXPECT_EQ(Int2::Ceil(input), Int2(2, -1));
	EXPECT_EQ(Int2::Round(input), Int2(2, -2));
	EXPECT_EQ(Int2::Zero() + value, value);
}

TEST(Int2Tests, ComponentArithmeticAndCompoundAssignments)
{
	const Int2 left{8, -9};
	const Int2 right{2, 3};
	EXPECT_EQ(+left, left);
	EXPECT_EQ(-left, (Int2{-8, 9}));
	EXPECT_EQ(left + right, (Int2{10, -6}));
	EXPECT_EQ(left - right, (Int2{6, -12}));
	EXPECT_EQ(left * right, (Int2{16, -27}));
	EXPECT_EQ(left / right, (Int2{4, -3}));
	EXPECT_EQ(2 * left, (Int2{16, -18}));
	Int2 value = left;
	value += right;
	EXPECT_EQ(value, (Int2{10, -6}));
	value -= right;
	EXPECT_EQ(value, left);
	value *= right;
	EXPECT_EQ(value, (Int2{16, -27}));
	value /= right;
	EXPECT_EQ(value, left);
	value *= 2;
	EXPECT_EQ(value, (Int2{16, -18}));
	value /= 2;
	EXPECT_EQ(value, left);
	EXPECT_EQ(left.Min(right), (Int2{2, -9}));
	EXPECT_EQ(left.Max(right), (Int2{8, 3}));
	EXPECT_EQ(left.Abs(), (Int2{8, 9}));
	EXPECT_EQ(left.Clamped({0, -5}, {5, 5}), (Int2{5, -5}));
	EXPECT_EQ(Int2::One(), Int2(1, 1));
	EXPECT_EQ(Int2::UnitX(), Int2(1, 0));
	EXPECT_EQ(Int2::UnitY(), Int2(0, 1));
}

TEST(Int2Tests, DoubleConversionsCoverSignedAndIntegerLimits)
{
	const Vector2d input{-1.5, 2.5};
	EXPECT_EQ(Int2::Floor(input), (Int2{-2, 2}));
	EXPECT_EQ(Int2::Ceil(input), (Int2{-1, 3}));
	EXPECT_EQ(Int2::Round(input), (Int2{-2, 3}));
	const Int2 limits{-2147483647 - 1, 2147483647};
	EXPECT_EQ(Int2::Round(limits.ToVector2<float64>()), limits);
	EXPECT_EQ((Int2{-7, 7} / 2), (Int2{-3, 3}));
}

TEST(Int2Tests, InvalidDivisionAndOverflowReportFatalContracts)
{
	EXPECT_DEATH(
	    {
		    suppressAssertionDialog();
		    (void)(Int2{1, 2} / Int2{1, 0});
	    },
	    "Integer vector divisors must be nonzero\\.");
	EXPECT_DEATH(
	    {
		    suppressAssertionDialog();
		    (void)(Int2{0, 2147483647} + Int2{0, 1});
	    },
	    "Integer vector result must be representable as int32\\.");
}
