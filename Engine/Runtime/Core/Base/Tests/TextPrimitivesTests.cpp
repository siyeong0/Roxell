#include <TextPrimitives.hpp>
#include <Types.h>

#include <gtest/gtest.h>

#include <cstring>
#include <system_error>

using namespace rox::core;

namespace
{
	int32 formatWithArguments(char* output, const usize capacity, const char* format, ...) noexcept
	{
		VaList arguments;
		va_start(arguments, format);
		const int32 result = FormatTextV(output, capacity, format, arguments);
		va_end(arguments);
		return result;
	}
}

TEST(TextPrimitivesTests, IntegerConversionReportsBoundariesAndErrors)
{
	char output[8]{};
	const ToCharsResult written = ToChars(output, output + sizeof(output), -1234);
	EXPECT_EQ(written.ec, std::errc{});
	EXPECT_EQ(written.ptr, output + 5);
	EXPECT_EQ(std::memcmp(output, "-1234", 5), 0);
	char shortOutput[2]{};
	const ToCharsResult shortWrite = ToChars(shortOutput, shortOutput + 2, 1234);
	EXPECT_EQ(shortWrite.ec, std::errc::value_too_large);
	EXPECT_EQ(shortWrite.ptr, shortOutput + 2);

	const char input[] = "42tail";
	int32 value = -1;
	const FromCharsResult parsed = FromChars(input, input + 6, value);
	EXPECT_EQ(parsed.ec, std::errc{});
	EXPECT_EQ(parsed.ptr, input + 2);
	EXPECT_EQ(value, 42);
	const char invalid[] = "x";
	value = 17;
	const FromCharsResult rejected = FromChars(invalid, invalid + 1, value);
	EXPECT_EQ(rejected.ec, std::errc::invalid_argument);
	EXPECT_EQ(rejected.ptr, invalid);
	EXPECT_EQ(value, 17);
}

TEST(TextPrimitivesTests, FormattingAndCStringOperations)
{
	char text[5]{};
	EXPECT_EQ(FormatText(text, sizeof(text), "%s-%d", "abc", 12), 6);
	EXPECT_STREQ(text, "abc-");
	EXPECT_EQ(CStringLength(text), 4u);
	EXPECT_LT(CompareCString("abc", "abd"), 0);
	EXPECT_EQ(CompareCString("abc", "abc"), 0);
	EXPECT_EQ(FindCStringChar(text, '-'), text + 3);
	EXPECT_EQ(FindCStringChar(text, 'z'), nullptr);
	wchar_t wide[8]{};
	EXPECT_EQ(FormatWideText(wide, 8, L"%d", 123), 3);
	EXPECT_STREQ(wide, L"123");
	char formatted[8]{};
	EXPECT_EQ(formatWithArguments(formatted, sizeof(formatted), "%s:%d", "id", 7), 4);
	EXPECT_STREQ(formatted, "id:7");
}
