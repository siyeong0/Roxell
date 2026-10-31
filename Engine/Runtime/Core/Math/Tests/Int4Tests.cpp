#include <Int4.hpp>

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

TEST(Int4Tests, ArithmeticAndConversionsRespectSignedLanes)
{
	const Int4 value{1, -2, 3, -4};
	EXPECT_EQ(value * 2, Int4(2, -4, 6, -8));
	EXPECT_EQ((value * 2) / 2, value);
	const Vector4f input{1.5f, -1.5f, 2.2f, -2.2f};
	EXPECT_EQ(Int4::Floor(input), Int4(1, -2, 2, -3));
	EXPECT_EQ(Int4::Ceil(input), Int4(2, -1, 3, -2));
	EXPECT_EQ(Int4::Round(input), Int4(2, -2, 2, -2));
	EXPECT_EQ(Int4::Zero() + value, value);
}

TEST(Int4Tests, ComponentArithmeticAndCompoundAssignments)
{
	const Int4 left{8, -9, 12, -10};
	const Int4 right{2, 3, -4, -5};
	EXPECT_EQ(+left, left);
	EXPECT_EQ(-left, (Int4{-8, 9, -12, 10}));
	EXPECT_EQ(left + right, (Int4{10, -6, 8, -15}));
	EXPECT_EQ(left - right, (Int4{6, -12, 16, -5}));
	EXPECT_EQ(left * right, (Int4{16, -27, -48, 50}));
	EXPECT_EQ(left / right, (Int4{4, -3, -3, 2}));
	EXPECT_EQ(2 * left, (Int4{16, -18, 24, -20}));
	Int4 value = left;
	value += right;
	EXPECT_EQ(value, (Int4{10, -6, 8, -15}));
	value -= right;
	EXPECT_EQ(value, left);
	value *= right;
	EXPECT_EQ(value, (Int4{16, -27, -48, 50}));
	value /= right;
	EXPECT_EQ(value, left);
	value *= 2;
	EXPECT_EQ(value, (Int4{16, -18, 24, -20}));
	value /= 2;
	EXPECT_EQ(value, left);
	EXPECT_EQ(left.Min(right), (Int4{2, -9, -4, -10}));
	EXPECT_EQ(left.Max(right), (Int4{8, 3, 12, -5}));
	EXPECT_EQ(left.Abs(), (Int4{8, 9, 12, 10}));
	EXPECT_EQ(left.Clamped({0, -5, 0, -8}, {5, 5, 10, 0}), (Int4{5, -5, 10, -8}));
	EXPECT_EQ(Int4::One(), Int4(1, 1, 1, 1));
	EXPECT_EQ(Int4::UnitX(), Int4(1, 0, 0, 0));
	EXPECT_EQ(Int4::UnitY(), Int4(0, 1, 0, 0));
	EXPECT_EQ(Int4::UnitZ(), Int4(0, 0, 1, 0));
	EXPECT_EQ(Int4::UnitW(), Int4(0, 0, 0, 1));
}

TEST(Int4Tests, DoubleConversionsCoverSignedAndIntegerLimits)
{
	const Vector4d input{-1.5, 2.5, -2.2, 0.0};
	EXPECT_EQ(Int4::Floor(input), (Int4{-2, 2, -3, 0}));
	EXPECT_EQ(Int4::Ceil(input), (Int4{-1, 3, -2, 0}));
	EXPECT_EQ(Int4::Round(input), (Int4{-2, 3, -2, 0}));
	const Int4 limits{-2147483647 - 1, 2147483647, 0, 1};
	EXPECT_EQ(Int4::Round(limits.ToVector4<float64>()), limits);
	EXPECT_EQ((Int4{-7, 7, -1, 1} / 2), (Int4{-3, 3, 0, 0}));
}

TEST(Int4Tests, InvalidDivisionAndOverflowReportFatalContracts)
{
	EXPECT_DEATH(
	    {
		    suppressAssertionDialog();
		    (void)(Int4{1, 2, 3, 4} / Int4{1, 1, 1, 0});
	    },
	    "Integer vector divisors must be nonzero\\.");
	EXPECT_DEATH(
	    {
		    suppressAssertionDialog();
		    (void)(Int4{0, 0, 0, 2147483647} + Int4{0, 0, 0, 1});
	    },
	    "Integer vector result must be representable as int32\\.");
}
