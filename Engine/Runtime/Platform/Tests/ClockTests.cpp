#include <IClock.h>

#include <gtest/gtest.h>

using namespace rox::os;

TEST(ClockTests, DefaultTimesAreZero)
{
	EXPECT_EQ(MonotonicTime{}.Nanoseconds, 0u);
	EXPECT_EQ(UtcTime{}.UnixMicroseconds, 0);
}

TEST(ClockTests, TimeComparisonsPreserveUnitsAndSignedUtcEpoch)
{
	EXPECT_EQ((MonotonicTime{42}), (MonotonicTime{42}));
	EXPECT_LT((MonotonicTime{41}), (MonotonicTime{42}));
	EXPECT_GT((MonotonicTime{43}), (MonotonicTime{42}));
	EXPECT_EQ((UtcTime{-17}), (UtcTime{-17}));
	EXPECT_LT((UtcTime{-17}), (UtcTime{0}));
	EXPECT_GT((UtcTime{1}), (UtcTime{0}));
}

namespace
{
	class FixedClock final : public IClock
	{
	public:
		MonotonicTime NowMonotonic() const noexcept override { return { 42 }; }
		UtcTime NowUtc() const noexcept override { return { -17 }; }
	};
}

TEST(ClockTests, InterfaceReturnsTheSuppliedMonotonicAndUtcValues)
{
	const FixedClock clock;
	const IClock& interface = clock;
	EXPECT_EQ(interface.NowMonotonic().Nanoseconds, 42u);
	EXPECT_EQ(interface.NowUtc().UnixMicroseconds, -17);
}
