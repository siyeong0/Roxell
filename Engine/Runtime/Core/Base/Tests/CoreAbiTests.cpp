#include <CoreAbi.h>

#include <gtest/gtest.h>

#include <limits>
#include <type_traits>

static_assert(sizeof(rox_int32) == 4);
static_assert(sizeof(rox_uint32) == 4);
static_assert(sizeof(rox_int64) == 8);
static_assert(sizeof(rox_uint64) == 8);
static_assert(std::is_signed_v<rox_int32>);
static_assert(std::is_signed_v<rox_int64>);
static_assert(std::is_unsigned_v<rox_uint32>);
static_assert(std::is_unsigned_v<rox_uint64>);
static_assert(std::is_same_v<rox_usize, size_t>);

TEST(CoreAbiTests, ScalarRangesMatchTheirDeclaredWidths)
{
	EXPECT_EQ(std::numeric_limits<rox_int32>::digits, 31);
	EXPECT_EQ(std::numeric_limits<rox_uint32>::digits, 32);
	EXPECT_EQ(std::numeric_limits<rox_int64>::digits, 63);
	EXPECT_EQ(std::numeric_limits<rox_uint64>::digits, 64);
}
