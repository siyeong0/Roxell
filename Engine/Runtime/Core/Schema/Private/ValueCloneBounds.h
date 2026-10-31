#pragma once

#include <ValueTraversal.h>

namespace rox::core
{
	[[nodiscard]] Result<void> ValidateValueCloneBounds(const Value& source, ValueCloneLimits limits = {}) noexcept;
}
