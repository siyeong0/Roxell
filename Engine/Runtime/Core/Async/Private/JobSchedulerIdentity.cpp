#include "JobSchedulerIdentity.h"
#include <Synchronization.hpp>
#include <MemoryOperations.hpp>
#include "JobState.h"
#include <Assertion.h>

namespace rox::core
{
	JobSchedulerIdentity::~JobSchedulerIdentity() noexcept
	{
		while (m_Free != nullptr)
		{
			FreeBlock* const block = m_Free;
			m_Free = block->Next;
			m_Resource->Deallocate(block, {sizeof(JobState), alignof(JobState)});
		}
	}

	void* JobSchedulerIdentity::Acquire(MemoryResource& resource) noexcept
	{
		ASSERT(m_Resource == nullptr || m_Resource == &resource, "Pooled job states must use the scheduler resource.");

		if (m_Resource == nullptr)
		{
			return resource.AllocateRequired({sizeof(JobState), alignof(JobState)});
		}

		{
			core::LockGuard lock(m_Mutex);

			if (m_Free != nullptr)
			{
				FreeBlock* const block = m_Free;
				m_Free = block->Next;
				core::DestroyAt(block);

				return block;
			}
		}

		return m_Resource->AllocateRequired({sizeof(JobState), alignof(JobState)});
	}

	void JobSchedulerIdentity::Recycle(void* storage, MemoryResource& resource) noexcept
	{
		if (m_Resource == nullptr)
		{
			resource.Deallocate(storage, {sizeof(JobState), alignof(JobState)});
			return;
		}

		core::LockGuard lock(m_Mutex);
		m_Free = core::ConstructAt(static_cast<FreeBlock*>(storage), FreeBlock{m_Free});
	}
}
