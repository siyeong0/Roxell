#pragma once

#include <Cancellation.h>
#include <Result.hpp>
#include <Synchronization.hpp>
#include <WindowsThreadBackend.h>

namespace rox::os
{
	// One process console subscription. The source must outlive this owner.
	class WindowsConsoleCancellation final
	{
	public:
		WindowsConsoleCancellation() noexcept = default;
		~WindowsConsoleCancellation() noexcept;
		WindowsConsoleCancellation(const WindowsConsoleCancellation&) = delete;
		WindowsConsoleCancellation& operator=(const WindowsConsoleCancellation&) = delete;

		[[nodiscard]] core::Result<void> Start(core::CancellationSource& source);

	private:
		bool m_bActive = false;
		void* m_OwnerThread = nullptr;

		WindowsThreadBackend m_Backend;
		core::NativeThread m_Worker;
		core::NativeThreadEntry m_WorkerEntry;

		core::CancellationSource* m_Source = nullptr;
		core::Atomic<bool> m_Stop = false;
	};
} // namespace rox::os
