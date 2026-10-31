#pragma once

#include <StringView.hpp>

namespace rox::core
{
	[[nodiscard]] bool IsValidUtf8(StringView text) noexcept;
} // namespace rox::core
