#include <Cancellation.h>
#include <Synchronization.hpp>
#include <Utility.hpp>

namespace rox::core
{
	bool CancellationToken::CanBeCancelled() const noexcept
	{
		return static_cast<bool>(m_State);
	}

	bool CancellationToken::IsCancellationRequested() const noexcept
	{
		return m_State && m_State->DispatchPhase.load(core::MemoryOrderAcquire) != detail::ECancellationDispatchPhase::Open;
	}

	CancellationToken::CancellationToken(SharedPtr<detail::CancellationState> state) noexcept
	    : m_State(core::Move(state))
	{
	}

}
