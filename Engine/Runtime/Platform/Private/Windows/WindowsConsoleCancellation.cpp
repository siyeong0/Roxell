#include <WindowsConsoleCancellation.h>
#include <ClockPrimitives.hpp>
#include <ThreadPrimitives.hpp>
#include <Synchronization.hpp>
#include <Vocabulary.hpp>
#include <Cancellation.h>
#include <Windows.h>

namespace rox::os
{
	namespace
	{
		rox::core::Atomic<bool> owned = false;
		rox::core::Atomic<bool> requested = false;

		BOOL WINAPI onControl(DWORD event)
		{
			if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
			{
				return FALSE;
			}

			requested.store(true, rox::core::MemoryOrderRelease);

			return TRUE;
		}
	} // namespace

	core::Result<void> WindowsConsoleCancellation::Start(core::CancellationSource& source)
	{
		if (m_bActive || !source.IsValid() || owned.exchange(true))
		{
			return core::Result<void>::Failure(core::EErrorCode::Conflict, "ROX-PLATFORM-CONSOLE-CANCELLATION-BUSY", "Console cancellation requires one valid process owner.");
		}

		requested.store(false);

		if (!SetConsoleCtrlHandler(&onControl, TRUE))
		{
			owned.store(false);

			return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-CONSOLE-CANCELLATION-INSTALL", "The console cancellation handler could not be installed.");
		}

		if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &m_OwnerThread, THREAD_TERMINATE, FALSE, 0))
		{
			SetConsoleCtrlHandler(&onControl, FALSE);
			owned.store(false);

			return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-CONSOLE-CANCELLATION-THREAD", "The calling thread could not be retained for cancellation.");
		}

		m_Source = &source;
		m_Stop.store(false, core::MemoryOrderRelease);
		m_WorkerEntry = { [](void* context) noexcept
						 {
							 WindowsConsoleCancellation& owner = *static_cast<WindowsConsoleCancellation*>(context);
							 while (!owner.m_Stop.load(core::MemoryOrderAcquire))
							 {
								 if (requested.load(core::MemoryOrderAcquire))
								 {
									 owner.m_Source->RequestCancellation();
									 while (!owner.m_Stop.load(core::MemoryOrderAcquire))
									 {
										 (void)CancelSynchronousIo(owner.m_OwnerThread);
										 core::SleepFor(core::Milliseconds(1));
									 }

									 return;
								 }

								 core::SleepFor(core::Milliseconds(1));
							 }
						 },
						 this };
		const core::NativeThreadStartResult started = m_Backend.Start(m_WorkerEntry);

		if (started.Result.Status != core::EThreadStartStatus::Started)
		{
			SetConsoleCtrlHandler(&onControl, FALSE);
			CloseHandle(m_OwnerThread);
			m_OwnerThread = nullptr;
			m_Source = nullptr;
			owned.store(false);

			return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-CONSOLE-CANCELLATION-WORKER", "The console cancellation worker could not start.");
		}

		m_Worker = started.Thread;

		m_bActive = true;

		return core::Result<void>::Success();
	}

	WindowsConsoleCancellation::~WindowsConsoleCancellation() noexcept
	{
		if (!m_bActive)
		{
			return;
		}

		SetConsoleCtrlHandler(&onControl, FALSE);
		m_Stop.store(true, core::MemoryOrderRelease);
		m_Backend.Join(m_Worker);
		CloseHandle(m_OwnerThread);
		owned.store(false);
	}
} // namespace rox::os
