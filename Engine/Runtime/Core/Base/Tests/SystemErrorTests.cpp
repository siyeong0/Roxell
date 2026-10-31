#include <SystemError.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(SystemErrorTests, DefaultAndSystemCategoryErrorsRemainDistinct)
{
	const SystemErrorCode empty{};
	const SystemErrorCode error(1, SystemCategory());
	EXPECT_FALSE(static_cast<bool>(empty));
	EXPECT_TRUE(static_cast<bool>(error));
	EXPECT_EQ(error.value(), 1);
	EXPECT_EQ(&error.category(), &SystemCategory());
	EXPECT_NE(error, empty);
}
