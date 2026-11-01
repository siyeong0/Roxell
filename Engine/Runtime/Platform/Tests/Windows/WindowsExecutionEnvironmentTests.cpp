#include <WindowsExecutionEnvironment.h>
#include <ScopeExit.hpp>
#include <gtest/gtest.h>

#include <cfenv>
#include <xmmintrin.h>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	TEST(WindowsExecutionEnvironmentTests, RepeatedReadsPreserveFloatingPointControls)
	{
		const unsigned int controls = _mm_getcsr();
		const int rounding = std::fegetround();
		const core::Result<os::WindowsExecutionEnvironment> first = os::ReadWindowsExecutionEnvironment();
		const core::Result<os::WindowsExecutionEnvironment> second = os::ReadWindowsExecutionEnvironment();
		ASSERT_TRUE(first);
		ASSERT_TRUE(second);
		EXPECT_TRUE(first.Value().Fingerprint.IsValid());
		EXPECT_EQ(first.Value().Fingerprint, second.Value().Fingerprint);
		EXPECT_EQ(_mm_getcsr(), controls);
		EXPECT_EQ(std::fegetround(), rounding);
	}

	TEST(WindowsExecutionEnvironmentTests, ControlChangesAffectFingerprintButStickyFlagsDoNot)
	{
		std::fenv_t savedEnvironment{};
		ASSERT_EQ(std::fegetenv(&savedEnvironment), 0);
		const unsigned int saved = _mm_getcsr();
		core::ScopeExit restore([&]() noexcept { (void)std::fesetenv(&savedEnvironment); _mm_setcsr(saved); });
		ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
		_mm_setcsr(0x1f80u);
		const core::Result<os::WindowsExecutionEnvironment> baseline = os::ReadWindowsExecutionEnvironment();
		ASSERT_TRUE(baseline);
		EXPECT_TRUE(baseline.Value().bDefaultFloatingPointMode);
		_mm_setcsr(0x1fbfu);
		const core::Result<os::WindowsExecutionEnvironment> sticky = os::ReadWindowsExecutionEnvironment();
		ASSERT_TRUE(sticky);
		EXPECT_EQ(sticky.Value().Fingerprint, baseline.Value().Fingerprint);
		EXPECT_EQ(_mm_getcsr(), 0x1fbfu);
		_mm_setcsr(0x3f80u);
		const core::Result<os::WindowsExecutionEnvironment> changed = os::ReadWindowsExecutionEnvironment();
		ASSERT_TRUE(changed);
		EXPECT_FALSE(changed.Value().bDefaultFloatingPointMode);
		EXPECT_NE(changed.Value().Fingerprint, baseline.Value().Fingerprint);
		EXPECT_EQ(_mm_getcsr(), 0x3f80u);
	}
}
