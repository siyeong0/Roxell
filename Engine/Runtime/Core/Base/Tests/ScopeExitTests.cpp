#include <ScopeExit.hpp>
#include <Types.h>

#include <gtest/gtest.h>

#include <utility>

using namespace rox::core;

TEST(ScopeExitTests, ScopeExitMovesAndReleasesResponsibility)
{
	int32 calls = 0;
	{
		ScopeExit original([&calls]() noexcept
		{
			++calls;
		});
		ScopeExit moved(std::move(original));
		ASSERT_FALSE(original.IsActive());
		ASSERT_TRUE(moved.IsActive());
	}
	ASSERT_EQ(calls, 1);
	{
		ScopeExit released([&calls]() noexcept
		{
			++calls;
		});
		released.Release();
		ASSERT_FALSE(released.IsActive());
	}
	ASSERT_EQ(calls, 1);
}
