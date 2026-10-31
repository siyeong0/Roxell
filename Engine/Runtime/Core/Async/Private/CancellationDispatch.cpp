#include "CancellationDispatch.h"
#include <Cancellation.h>
#include <Assertion.h>

namespace rox::core::detail
{
	namespace
	{
		struct CallbackFrame final
		{
			CancellationRegistration* Registration;
			CallbackFrame* Previous;
		};

		thread_local CallbackFrame* s_CallbackFrame = nullptr;
	}

	[[nodiscard]] bool IsInvokingOnCurrentThread(CancellationRegistration* const registration) noexcept
	{
		for (CallbackFrame* frame = s_CallbackFrame; frame != nullptr; frame = frame->Previous)
		{
			if (frame->Registration == registration)
			{
				return true;
			}
		}

		return false;
	}

	void InvokeCallback(CancellationRegistration* const registration, const CancellationCallback callback,
	                    void* const context) noexcept
	{
		ASSERT(registration != nullptr, "Cancellation callback dispatch requires a valid registration.");
		ASSERT(callback != nullptr, "Cancellation callback dispatch requires a valid callback.");

		CallbackFrame frame{registration, s_CallbackFrame};
		s_CallbackFrame = &frame;
		callback(context);
		s_CallbackFrame = frame.Previous;
	}

	bool IsDispatchingOnCurrentThread() noexcept
	{
		return s_CallbackFrame != nullptr;
	}
}
