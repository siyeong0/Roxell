#include <Types.h>
#include <TypeTraits.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

static_assert(sizeof(int8) == 1 && sizeof(uint8) == 1);
static_assert(sizeof(int16) == 2 && sizeof(uint16) == 2);
static_assert(sizeof(int32) == 4 && sizeof(uint32) == 4);
static_assert(sizeof(int64) == 8 && sizeof(uint64) == 8);
static_assert(IsSigned<int32> && !IsSigned<uint32>);
static_assert(IsFloatingPoint<float32> && IsFloatingPoint<float64>);

TEST(TypesTests, NullptrAliasRepresentsNullPointer)
{
	const Nullptr value = nullptr;
	EXPECT_EQ(value, nullptr);
}
