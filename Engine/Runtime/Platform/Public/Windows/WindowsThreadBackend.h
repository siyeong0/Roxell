#pragma once

#include <IThreadBackend.h>

namespace rox::os
{
	class WindowsThreadBackend final : public core::IThreadBackend
	{
	public:
		[[nodiscard]] core::NativeThreadStartResult Start(core::NativeThreadEntry& entry) noexcept override;
		void Join(core::NativeThread thread) noexcept override;
		[[nodiscard]] core::ThreadId CurrentThreadId() const noexcept override;
	};
}
