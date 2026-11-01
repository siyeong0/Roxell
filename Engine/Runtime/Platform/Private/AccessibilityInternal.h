#pragma once

#include <Accessibility.h>
#include <ClockPrimitives.hpp>
#include <Synchronization.hpp>
#include <Utility.hpp>

namespace rox::os
{
	struct AccessibilityActionState
	{
		AccessibilityAction Action;

		rox::core::Mutex Mutex;
		rox::core::ConditionVariable Changed;

		rox::core::MonotonicClock::time_point Deadline;
		core::EErrorCode Error = core::EErrorCode::None;

		void (*OnSuccess)(void*) noexcept = nullptr;
		void* SuccessContext = nullptr;

		bool bStarted = false;
		bool bComplete = false;

		explicit AccessibilityActionState(core::MemoryResource& memory) noexcept
			: Action(memory)
			, Deadline(rox::core::MonotonicClock::now() + rox::core::Seconds(2))
		{
		}
	};

	struct WindowsAccessibilityAccess
	{
		static AccessibilityRequest Make(core::SharedPtr<AccessibilityActionState> state) noexcept
		{
			AccessibilityRequest result;
			result.m_State = rox::core::Move(state);

			return result;
		}
	};
}
