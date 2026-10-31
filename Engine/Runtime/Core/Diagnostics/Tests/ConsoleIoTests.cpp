#include <ConsoleIo.hpp>

#include <cstdlib>
#include <gtest/gtest.h>

using namespace rox::core;

TEST(ConsoleIoTests, PrintErrorFormatsTextAndReportsCharacterCount)
{
	EXPECT_EXIT(
	    {
		    const int count = PrintError("value=%d", 42);
		    std::exit(count == 8 ? 0 : 1);
	    },
	    ::testing::ExitedWithCode(0), "value=42");
}

TEST(ConsoleIoTests, PutErrorTextWritesTextAndReportsSuccess)
{
	EXPECT_EXIT(
	    {
		    const int result = PutErrorText("console error text");
		    std::exit(result != END_OF_FILE ? 0 : 1);
	    },
	    ::testing::ExitedWithCode(0), "console error text");
}
