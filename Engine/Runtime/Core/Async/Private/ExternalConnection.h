#pragma once

#include "TaskCompletion.h"

#include <Cancellation.h>
#include <TaskHandle.h>

namespace rox::core { struct JobSchedulerState; }

namespace rox::core::detail
{
	struct ExternalConnection final
	{
		void Close(bool cancelled) noexcept;
		void Retire() noexcept;
		void RequestRetirement() noexcept;
		static void Reclaim(void* context) noexcept;

		Atomic<JobSchedulerState*> Owner = nullptr;
		Atomic<bool> bRetirementClaimed = false;
		Atomic<bool> bRetired = false;
		detail::DeferredCleanup Cleanup;
		SharedPtr<detail::TaskRetirement> Retirement;

		detail::ExternalConnection* Previous = nullptr;
		Atomic<bool> bCancelled = false;
		Atomic<bool> bClosed = false;
		Atomic<bool> bClosing = false;

		TaskHandle Gate;
		UniqueFunction<void() noexcept> Complete;
		UniqueFunction<void() noexcept> Disconnect;
		CancellationRegistration Registration;
		SharedPtr<detail::ExternalConnection> Next;
	};
}
