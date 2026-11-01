#include <WindowsExecutionEnvironment.h>
#include <ScopeExit.hpp>
#include <gtest/gtest.h>

#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <crtdbg.h>

namespace
{
	thread_local unsigned int s_AllocationAttempts = 0;
	thread_local bool s_bAllocationProbeActive = false;

	int allocationHook(int operation, void*, size_t, int block, long, const unsigned char*, int)
	{
		if (s_bAllocationProbeActive && operation == _HOOK_ALLOC && block != _CRT_BLOCK)
		{
			++s_AllocationAttempts;
			return 0;
		}
		return 1;
	}

	class AllocationScope final
	{
	public:
		AllocationScope() noexcept
			: m_Previous(_CrtSetAllocHook(allocationHook))
		{
			s_AllocationAttempts = 0;
			s_bAllocationProbeActive = true;
		}

		~AllocationScope()
		{
			s_bAllocationProbeActive = false;
			_CrtSetAllocHook(m_Previous);
		}

	private:
		_CRT_ALLOC_HOOK m_Previous;
	};
}
#endif

#include <cfenv>
#include <xmmintrin.h>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	TEST(WindowsExecutionEnvironmentRoundingTests, ControlMatrixChangesFingerprintWithoutMutatingControls)
	{
		std::fenv_t savedEnvironment{};
		ASSERT_EQ(std::fegetenv(&savedEnvironment), 0);
		const unsigned int saved = _mm_getcsr();
		core::ScopeExit restore([&]() noexcept { (void)std::fesetenv(&savedEnvironment); _mm_setcsr(saved); });
		ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
		_mm_setcsr(0x1f80u);
		const core::Result<os::WindowsExecutionEnvironment> baseline = os::ReadWindowsExecutionEnvironment();
		ASSERT_TRUE(baseline);
		ASSERT_TRUE(baseline.Value().bDefaultFloatingPointMode);
		ASSERT_TRUE(baseline.Value().Fingerprint.IsValid());
		for (const unsigned int control : { 0x1f80u, 0x1fbfu, 0x3f80u, 0x5f80u, 0x7f80u, 0x9f80u, 0x1fc0u, 0x1f00u })
		{
			_mm_setcsr(control);
			const int rounding = std::fegetround();
			const core::Result<os::WindowsExecutionEnvironment> observed = os::ReadWindowsExecutionEnvironment();
			const bool bUnchanged = _mm_getcsr() == control && std::fegetround() == rounding;
			_mm_setcsr(0x1f80u);
			ASSERT_TRUE(observed) << control;
			EXPECT_TRUE(bUnchanged) << control;
			const bool bDefaultMode = (control & 0xffc0u) == 0x1f80u;
			EXPECT_EQ(observed.Value().bDefaultFloatingPointMode, bDefaultMode) << control;
			EXPECT_EQ(observed.Value().Fingerprint == baseline.Value().Fingerprint, bDefaultMode) << control;
		}
	}

	TEST(WindowsExecutionEnvironmentRoundingTests, DirectedRoundingChangesFingerprintWithoutMutatingState)
	{
		std::fenv_t savedEnvironment{};
		ASSERT_EQ(std::fegetenv(&savedEnvironment), 0);
		const unsigned int saved = _mm_getcsr();
		core::ScopeExit restore([&]() noexcept { (void)std::fesetenv(&savedEnvironment); _mm_setcsr(saved); });
		ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
		_mm_setcsr(0x1f80u);
		const core::Result<os::WindowsExecutionEnvironment> baseline = os::ReadWindowsExecutionEnvironment();
		ASSERT_TRUE(baseline);
		for (const int mode : { FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO })
		{
			ASSERT_EQ(std::fesetround(mode), 0);
			const unsigned int before = _mm_getcsr();
			const core::Result<os::WindowsExecutionEnvironment> observed = os::ReadWindowsExecutionEnvironment();
			const bool bUnchanged = _mm_getcsr() == before && std::fegetround() == mode;
			ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
			ASSERT_TRUE(observed) << mode;
			EXPECT_TRUE(bUnchanged) << mode;
			EXPECT_FALSE(observed.Value().bDefaultFloatingPointMode) << mode;
			EXPECT_NE(observed.Value().Fingerprint, baseline.Value().Fingerprint) << mode;
		}
	}

#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
	TEST(WindowsExecutionEnvironmentRoundingTests, RepeatedReadsDoNotAllocateThroughSystemResource)
	{
		const core::Result<os::WindowsExecutionEnvironment> baseline = os::ReadWindowsExecutionEnvironment();
		ASSERT_TRUE(baseline);

		bool bValid = true;
		{
			AllocationScope scope;
			for (core::uint32 index = 0; index < 128; ++index)
			{
				const core::Result<os::WindowsExecutionEnvironment> observed = os::ReadWindowsExecutionEnvironment();
				bValid &= observed && observed.Value().Fingerprint == baseline.Value().Fingerprint;
			}
		}
		EXPECT_TRUE(bValid);
		EXPECT_EQ(s_AllocationAttempts, 0u);
	}
#endif
} // namespace
