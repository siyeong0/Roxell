#include "TaskCompletion.h"
#include <Cancellation.h>
#include <Synchronization.hpp>
#include <Task.hpp>
#include <Types.h>
#include "CancellationDispatch.h"

namespace rox::core
{
	CancellationRegistration::~CancellationRegistration()
	{
		Reset();
	}

	ECancellationRegistrationResult CancellationRegistration::Register(const CancellationToken& token, const CancellationCallback callback, void* const context) noexcept
	{
		detail::CleanupScope cleanup;
		ASSERT(callback != nullptr, "Cancellation registration requires a valid callback.");

		acquireOperation();
		resetInternal();

		m_State = token.m_State;
		m_Node.Callback = callback;
		m_Node.Context = context;

		if (!m_State)
		{
			m_Node.Callback = nullptr;
			m_Node.Context = nullptr;
			releaseOperation();

			return ECancellationRegistrationResult::NotCancellable;
		}

		detail::CancellationState* const state = m_State.Get();
		bool bRegistered = false;
		{
			rox::core::LockGuard lock(state->Mutex);

			if (state->DispatchPhase.load(rox::core::MemoryOrderAcquire) == detail::ECancellationDispatchPhase::Open)
			{
				m_Node.Next = state->RegistrationHead;

				if (m_Node.Next != nullptr)
				{
					m_Node.Next->Previous = &m_Node;
				}

				state->RegistrationHead = &m_Node;
				m_Node.Phase.store(detail::ECancellationNodePhase::Registered, rox::core::MemoryOrderRelease);
				bRegistered = true;
			}
			else
			{
				m_Node.Phase.store(detail::ECancellationNodePhase::Invoking, rox::core::MemoryOrderRelease);
			}
		}

		if (bRegistered)
		{
			releaseOperation();

			return ECancellationRegistrationResult::Registered;
		}

		detail::InvokeCallback(this, callback, context);
		m_Node.Phase.store(detail::ECancellationNodePhase::Completed, rox::core::MemoryOrderRelease);
		state->RegistrationChangeSequence.fetch_add(1, rox::core::MemoryOrderRelease);
		state->RegistrationChangeSequence.notify_all();
		m_State.Reset();
		m_Node.Callback = nullptr;
		m_Node.Context = nullptr;
		m_Node.Phase.store(detail::ECancellationNodePhase::Idle, rox::core::MemoryOrderRelease);
		releaseOperation();

		return ECancellationRegistrationResult::InvokedSynchronously;
	}

	void CancellationRegistration::Reset() noexcept
	{
		acquireOperation();
		resetInternal();
		releaseOperation();
	}

	void CancellationRegistration::acquireOperation() noexcept
	{
		bool bExpected = false;
		const bool bAcquired = m_bOperationActive.compare_exchange_strong(bExpected, true, rox::core::MemoryOrderAcqRel,
		                                                                  rox::core::MemoryOrderAcquire);
		ASSERT(bAcquired, "CancellationRegistration operations require external synchronization.");
	}

	void CancellationRegistration::releaseOperation() noexcept
	{
		const bool bWasActive = m_bOperationActive.exchange(false, rox::core::MemoryOrderRelease);
		ASSERT(bWasActive, "CancellationRegistration operation state is invalid.");
	}

	void CancellationRegistration::resetInternal() noexcept
	{
		ASSERT(!detail::IsInvokingOnCurrentThread(this), "A cancellation callback cannot reset or destroy its own registration.");

		SharedPtr<detail::CancellationState> state = m_State;
		detail::ECancellationNodePhase phase = m_Node.Phase.load(rox::core::MemoryOrderAcquire);

		if (phase == detail::ECancellationNodePhase::Registered)
		{
			ASSERT(state, "A registered cancellation callback must own its state.");

			rox::core::LockGuard lock(state->Mutex);
			phase = m_Node.Phase.load(rox::core::MemoryOrderAcquire);

			if (phase == detail::ECancellationNodePhase::Registered)
			{
				m_Node.Unlink(*state);
				m_Node.Phase.store(detail::ECancellationNodePhase::Idle, rox::core::MemoryOrderRelease);
				phase = detail::ECancellationNodePhase::Idle;
			}
		}

		while (phase == detail::ECancellationNodePhase::Invoking)
		{
			ASSERT(!detail::IsDispatchingOnCurrentThread(), "Cancellation callbacks cannot wait for another in-flight registration.");
			ASSERT(state, "An invoking cancellation callback must own its state.");

			const rox::core::uint64 changeSequence = state->RegistrationChangeSequence.load(rox::core::MemoryOrderAcquire);
			phase = m_Node.Phase.load(rox::core::MemoryOrderAcquire);

			if (phase == detail::ECancellationNodePhase::Invoking)
			{
				state->RegistrationChangeSequence.wait(changeSequence, rox::core::MemoryOrderAcquire);
				phase = m_Node.Phase.load(rox::core::MemoryOrderAcquire);
			}
		}

		ASSERT(phase == detail::ECancellationNodePhase::Idle || phase == detail::ECancellationNodePhase::Completed, "CancellationRegistration has an invalid terminal phase.");

		m_State.Reset();
		m_Node.Callback = nullptr;
		m_Node.Context = nullptr;
		m_Node.Previous = nullptr;
		m_Node.Next = nullptr;
		m_Node.Phase.store(detail::ECancellationNodePhase::Idle, rox::core::MemoryOrderRelease);
	}

	void detail::CancellationNode::Unlink(CancellationState& state) noexcept
	{
		if (Previous != nullptr)
		{
			Previous->Next = Next;
		}
		else
		{
			ASSERT(state.RegistrationHead == this, "CancellationRegistration head linkage is invalid.");

			state.RegistrationHead = Next;
		}

		if (Next != nullptr)
		{
			Next->Previous = Previous;
		}

		Previous = nullptr;
		Next = nullptr;
	}
}
