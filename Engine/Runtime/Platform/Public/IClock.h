#pragma once

#include <ComparisonTypes.hpp>
#include <Types.h>

#include <Vocabulary.hpp>

namespace rox::os
{
	struct MonotonicTime
	{
		core::uint64 Nanoseconds = 0;

		core::StrongOrdering operator<=>(const MonotonicTime&) const = default;
	};

	struct UtcTime
	{
		core::int64 UnixMicroseconds = 0;

		core::StrongOrdering operator<=>(const UtcTime&) const = default;
	};

	class IClock
	{
	public:
		virtual ~IClock() = default;

		[[nodiscard]] virtual MonotonicTime NowMonotonic() const noexcept = 0;
		[[nodiscard]] virtual UtcTime NowUtc() const noexcept = 0;
	};
} // namespace rox::os
