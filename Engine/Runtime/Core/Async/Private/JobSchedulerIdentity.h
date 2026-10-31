#pragma once

#include <MemoryResource.h>
#include <Synchronization.hpp>
#include <mutex>

namespace rox::core
{
	class JobSchedulerIdentity final
	{
	public:
		JobSchedulerIdentity() noexcept = default;

		explicit JobSchedulerIdentity(MemoryResource& resource) noexcept
		    : m_Resource(&resource)
		{
		}

		~JobSchedulerIdentity() noexcept;
		void* Acquire(MemoryResource& resource) noexcept;
		void Recycle(void* storage, MemoryResource& resource) noexcept;

	private:
		struct FreeBlock
		{
			FreeBlock* Next;
		};

		MemoryResource* m_Resource = nullptr;
		rox::core::Mutex m_Mutex;
		FreeBlock* m_Free = nullptr;
	};

}
