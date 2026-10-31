#pragma once

#include <cerrno>
#include <csetjmp>

namespace rox::core
{
	using JumpBuffer = std::jmp_buf;

	inline int NativeErrorNumber() noexcept { return errno; }

	inline constexpr int NativeOutOfMemory = ENOMEM;
}
