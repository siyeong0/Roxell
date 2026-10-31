#include <FloatEnvironment.hpp>

#include <gtest/gtest.h>

#include <cfenv>

namespace
{
	class EnvironmentGuard final
	{
	public:
		EnvironmentGuard() noexcept
			: m_bSaved(std::fegetenv(&m_Environment) == 0)
		{
		}

		~EnvironmentGuard()
		{
			if (m_bSaved)
			{
				EXPECT_EQ(std::fesetenv(&m_Environment), 0);
			}
		}

		bool IsSaved() const noexcept
		{
			return m_bSaved;
		}

	private:
		std::fenv_t m_Environment{};
		bool m_bSaved;
	};
}

TEST(FloatEnvironmentTests, HoldClearsExceptionsAndSetRestoresEnvironment)
{
	EnvironmentGuard guard;
	if (!guard.IsSaved())
	{
		GTEST_SKIP() << "Floating-point environment is unavailable";
	}

	if (std::feclearexcept(FE_ALL_EXCEPT) != 0 || std::feraiseexcept(FE_INVALID) != 0 ||
		(std::fetestexcept(FE_INVALID) & FE_INVALID) == 0)
	{
		GTEST_SKIP() << "Floating-point invalid exception is unavailable";
	}

	const int rounding = rox::core::GetRoundingMode();
	rox::core::FloatEnvironment held{};
	if (rox::core::HoldFloatExceptions(&held) != 0)
	{
		GTEST_SKIP() << "Holding floating-point exceptions is unavailable";
	}

	EXPECT_EQ(std::fetestexcept(FE_ALL_EXCEPT), 0);
	EXPECT_EQ(rox::core::GetRoundingMode(), rounding);
	ASSERT_EQ(rox::core::SetFloatEnvironment(&held), 0);
	EXPECT_NE(std::fetestexcept(FE_INVALID) & FE_INVALID, 0);
	EXPECT_EQ(rox::core::GetRoundingMode(), rounding);
}
