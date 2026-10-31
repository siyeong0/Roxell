#pragma once

#include <cstdio>
#include <Types.h>
#include <Utility.hpp>
#include <utility>

namespace rox::core
{
	template <typename... T> int PrintError(const char* format, T&&... arguments) noexcept
	{
		return std::fprintf(stderr, format, rox::core::Forward<T>(arguments)...);
	}

	template <typename... T> int Print(const char* format, T&&... arguments) noexcept
	{
		return std::fprintf(stdout, format, rox::core::Forward<T>(arguments)...);
	}

	inline int PutText(const char* text) noexcept { return std::fputs(text, stdout); }
	inline int PutErrorText(const char* text) noexcept { return std::fputs(text, stderr); }
	inline int PutCharacter(int character) noexcept { return std::fputc(character, stdout); }
	inline int FlushOutput() noexcept { return std::fflush(stdout); }
	inline int OutputError() noexcept { return std::ferror(stdout); }
	inline int InputError() noexcept { return std::ferror(stdin); }

	inline rox::core::usize WriteOutput(const void* data, rox::core::usize size, rox::core::usize count) noexcept
	{
		return std::fwrite(data, size, count, stdout);
	}

	inline rox::core::usize ReadInput(void* data, rox::core::usize size, rox::core::usize count) noexcept
	{
		return std::fread(data, size, count, stdin);
	}

	inline constexpr int END_OF_FILE = EOF;
}
