#include <Int3.hpp>

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

TEST(Int3Tests, ArithmeticAndConversionsRespectSignedLanes)
{
	const Int3 value{1, -2, 3};
	EXPECT_EQ(value * 2, Int3(2, -4, 6));
	EXPECT_EQ((value * 2) / 2, value);
	const Vector3f input{1.5f, -1.5f, 2.2f};
	EXPECT_EQ(Int3::Floor(input), Int3(1, -2, 2));
	EXPECT_EQ(Int3::Ceil(input), Int3(2, -1, 3));
	EXPECT_EQ(Int3::Round(input), Int3(2, -2, 2));
	EXPECT_EQ(Int3::Zero() + value, value);
}

TEST(Int3Tests, ComponentArithmeticAndCompoundAssignments)
{
	const Int3 left{8, -9, 12};
	const Int3 right{2, 3, -4};
	EXPECT_EQ(+left, left);
	EXPECT_EQ(-left, (Int3{-8, 9, -12}));
	EXPECT_EQ(left + right, (Int3{10, -6, 8}));
	EXPECT_EQ(left - right, (Int3{6, -12, 16}));
	EXPECT_EQ(left * right, (Int3{16, -27, -48}));
	EXPECT_EQ(left / right, (Int3{4, -3, -3}));
	EXPECT_EQ(2 * left, (Int3{16, -18, 24}));
	Int3 value = left;
	value += right;
	EXPECT_EQ(value, (Int3{10, -6, 8}));
	value -= right;
	EXPECT_EQ(value, left);
	value *= right;
	EXPECT_EQ(value, (Int3{16, -27, -48}));
	value /= right;
	EXPECT_EQ(value, left);
	value *= 2;
	EXPECT_EQ(value, (Int3{16, -18, 24}));
	value /= 2;
	EXPECT_EQ(value, left);
	EXPECT_EQ(left.Min(right), (Int3{2, -9, -4}));
	EXPECT_EQ(left.Max(right), (Int3{8, 3, 12}));
	EXPECT_EQ(left.Abs(), (Int3{8, 9, 12}));
	EXPECT_EQ(left.Clamped({0, -5, 0}, {5, 5, 10}), (Int3{5, -5, 10}));
	EXPECT_EQ(Int3::One(), Int3(1, 1, 1));
	EXPECT_EQ(Int3::UnitX(), Int3(1, 0, 0));
	EXPECT_EQ(Int3::UnitY(), Int3(0, 1, 0));
	EXPECT_EQ(Int3::UnitZ(), Int3(0, 0, 1));
}

TEST(Int3Tests, DoubleConversionsCoverSignedAndIntegerLimits)
{
	const Vector3d input{-1.5, 2.5, -2.2};
	EXPECT_EQ(Int3::Floor(input), (Int3{-2, 2, -3}));
	EXPECT_EQ(Int3::Ceil(input), (Int3{-1, 3, -2}));
	EXPECT_EQ(Int3::Round(input), (Int3{-2, 3, -2}));
	const Int3 limits{-2147483647 - 1, 2147483647, 0};
	EXPECT_EQ(Int3::Round(limits.ToVector3<float64>()), limits);
	EXPECT_EQ((Int3{-7, 7, -1} / 2), (Int3{-3, 3, 0}));
}

TEST(Int3Tests, InvalidDivisionAndOverflowReportFatalContracts)
{
	EXPECT_DEATH(
	    {
		    suppressAssertionDialog();
		    (void)(Int3{1, 2, 3} / Int3{1, 1, 0});
	    },
	    "Integer vector divisors must be nonzero\\.");
	EXPECT_DEATH(
	    {
		    suppressAssertionDialog();
		    (void)(Int3{0, 0, 2147483647} + Int3{0, 0, 1});
	    },
	    "Integer vector result must be representable as int32\\.");
}
