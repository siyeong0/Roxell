#include "TaskCompletion.h"
#include <Cancellation.h>
#include <Synchronization.hpp>
#include <Task.hpp>
#include <SystemMemoryResource.h>
#include "CancellationDispatch.h"

namespace rox::core
{
	CancellationSource::CancellationSource() noexcept
	    : CancellationSource(SystemMemoryResource::Get())
	{
	}

	CancellationSource CancellationSource::Create(MemoryResource& resource) noexcept
	{
		ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "CancellationSource requires a thread-safe memory resource.");

		CancellationSource source(EmptyTag{});
		static_cast<void>(MakeSharedInto<detail::CancellationState>(resource, &source.m_State));

		return source;
	}

	CancellationSource::CancellationSource(MemoryResource& resource) noexcept
	{
		ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "CancellationSource requires a thread-safe memory resource.");

		m_State = MakeSharedRequired<detail::CancellationState>(resource);
	}

	CancellationSource::CancellationSource(EmptyTag) noexcept
	{
	}

	bool CancellationSource::IsValid() const noexcept
	{
		return static_cast<bool>(m_State);
	}

	CancellationToken CancellationSource::GetToken() const noexcept
	{
		return CancellationToken(m_State);
	}

	void CancellationSource::RequestCancellation() noexcept
	{
		detail::CleanupScope cleanup;
		SharedPtr<detail::CancellationState> state = m_State;

		if (!state)
		{
			return;
		}

		detail::ECancellationDispatchPhase expected = detail::ECancellationDispatchPhase::Open;
		const bool bDispatchClaimed = state->DispatchPhase.compare_exchange_strong(expected, detail::ECancellationDispatchPhase::Dispatching, rox::core::MemoryOrderAcqRel, rox::core::MemoryOrderAcquire);

		if (!bDispatchClaimed)
		{
			if (expected == detail::ECancellationDispatchPhase::Dispatching && !detail::IsDispatchingOnCurrentThread())
			{
				state->DispatchPhase.wait(detail::ECancellationDispatchPhase::Dispatching, rox::core::MemoryOrderAcquire);
			}

			return;
		}

		for (;;)
		{
			detail::CancellationNode* node = nullptr;
			CancellationCallback callback = nullptr;
			void* context = nullptr;
			{
				rox::core::LockGuard lock(state->Mutex);
				node = state->RegistrationHead;

				if (node == nullptr)
				{
					break;
				}

				node->Unlink(*state);
				node->Phase.store(detail::ECancellationNodePhase::Invoking, rox::core::MemoryOrderRelease);
				callback = node->Callback;
				context = node->Context;
			}

			detail::InvokeCallback(node->Owner, callback, context);
			node->Phase.store(detail::ECancellationNodePhase::Completed, rox::core::MemoryOrderRelease);
			state->RegistrationChangeSequence.fetch_add(1, rox::core::MemoryOrderRelease);
			state->RegistrationChangeSequence.notify_all();
		}

		state->DispatchPhase.store(detail::ECancellationDispatchPhase::Complete, rox::core::MemoryOrderRelease);
		state->DispatchPhase.notify_all();
	}

}
