#pragma once

#include <IClock.h>
#include <Result.hpp>
#include <Types.h>

namespace rox::os
{
	class WindowsClock final : public IClock
	{
	public:
		[[nodiscard]] static core::Result<WindowsClock> Create();

		[[nodiscard]] MonotonicTime NowMonotonic() const noexcept override;
		[[nodiscard]] UtcTime NowUtc() const noexcept override;

	private:
		WindowsClock(core::int64 frequency, core::int64 originTicks) noexcept;

	private:
		core::int64 m_Frequency;
		core::int64 m_OriginTicks;
	};
} // namespace rox::os
