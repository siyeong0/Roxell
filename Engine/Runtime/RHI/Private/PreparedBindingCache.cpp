#include "PreparedBindingCache.h"

#if ROXELL_RHI_BACKEND == 1
#include "D3D12/D3D12Backend.h"
#elif ROXELL_RHI_BACKEND == 2
#include "Vulkan/VulkanBackend.h"
#endif

#include <Assertion.h>

namespace rox::rhi
{
	using namespace core;

	PreparedDescriptorSet::PreparedDescriptorSet(SelectedBackendState* backend, void* native, EPreparedDescriptorStorage storage) noexcept
		: m_Backend(backend)
		, m_Native(native)
		, m_Storage(storage)
	{
		ASSERT(backend && native, "Native descriptors retain their backend destruction contract.");
	}

	PreparedDescriptorSet::~PreparedDescriptorSet() noexcept
	{
#if ROXELL_RHI_BACKEND == 1
		d3d12::DestroyDescriptorSetRealization(m_Backend, m_Native);
#elif ROXELL_RHI_BACKEND == 2
		if (m_Storage == EPreparedDescriptorStorage::TableSnapshot)
		{
			vulkan::DestroyDescriptorTable(m_Backend, m_Native);
		}
		else
		{
			vulkan::DestroyDescriptorSetRealization(m_Backend, m_Native);
		}
#else
		ASSERT(false, "The Null backend does not create prepared native owners.");
#endif
	}

	const SharedPtr<PreparedDescriptorSet>& PreparedBindingCache::Get(uint32 index) const noexcept
	{
		ASSERT(index < CAPACITY, "The immutable descriptor cache has bounded entries.");

		return m_Entries[index];
	}

	void PreparedBindingCache::Capture(const SharedPtr<PreparedDescriptorSet>& owner) noexcept
	{
		ASSERT(owner, "Only a complete native descriptor realization enters the cache.");
		for (SharedPtr<PreparedDescriptorSet>& entry : m_Entries)
		{
			if (!entry)
			{
				entry = owner;

				return;
			}
		}
		for (SharedPtr<PreparedDescriptorSet>& entry : m_Entries)
		{
			if (entry.UseCount() == 1)
			{
				entry = owner;

				return;
			}
		}
	}
}
