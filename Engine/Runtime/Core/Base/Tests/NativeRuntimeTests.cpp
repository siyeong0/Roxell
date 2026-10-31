#include <NativeRuntime.hpp>

#include <gtest/gtest.h>

#include <cerrno>

using namespace rox::core;

namespace
{
	class ErrorNumberRestorer final
	{
	public:
		ErrorNumberRestorer() noexcept : m_Original(errno) {}
		~ErrorNumberRestorer() noexcept
		{
			errno = m_Original;
		}

	private:
		int m_Original;
	};
}

TEST(NativeRuntimeTests, NativeErrorNumberReadsCurrentThreadError)
{
	const ErrorNumberRestorer restore;
	errno = 0;
	EXPECT_EQ(NativeErrorNumber(), 0);
	errno = NativeOutOfMemory;
	EXPECT_EQ(NativeErrorNumber(), ENOMEM);
}
