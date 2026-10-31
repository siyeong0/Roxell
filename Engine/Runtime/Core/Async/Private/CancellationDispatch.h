#pragma once

#include <Cancellation.h>

namespace rox::core::detail
{
	bool IsInvokingOnCurrentThread(CancellationRegistration* registration) noexcept;
	bool IsDispatchingOnCurrentThread() noexcept;
	void InvokeCallback(CancellationRegistration* registration, CancellationCallback callback, void* context) noexcept;
}
