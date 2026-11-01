#pragma once

#include <LinearMemoryResource.h>
#include <MemoryBlock.h>
#include <Utility.hpp>
#include <Vocabulary.hpp>

namespace rox::core
{
	// Caller-confined storage. Initialize once; destroy every suballocation before this owner.
	class ScratchMemory final
	{
	public:
		ScratchMemory() noexcept = default;

		// Adopts already allocated storage; an empty block creates a valid zero-capacity arena.
		explicit ScratchMemory(MemoryBlock&& backing) noexcept
		    : m_Backing(core::Move(backing))
		{
			m_Resource.emplace(m_Backing.GetData(), m_Backing.GetByteCount());
		}

		ScratchMemory(const ScratchMemory&) = delete;
		ScratchMemory& operator=(const ScratchMemory&) = delete;
		ScratchMemory(ScratchMemory&&) = delete;
		ScratchMemory& operator=(ScratchMemory&&) = delete;

		// On exhaustion remains uninitialized and can be retried. Zero capacity succeeds.
		[[nodiscard]] bool Initialize(MemoryResource& upstream, usize capacityByteCount) noexcept
		{
			ASSERT(!m_Resource.has_value(), "Scratch memory can only be initialized once.");
			if (!MemoryBlock::CreateInto(upstream, &m_Backing, {capacityByteCount, DEFAULT_MEMORY_ALIGNMENT}))
			{
				return false;
			}
			m_Resource.emplace(m_Backing.GetData(), capacityByteCount);
			return true;
		}

		[[nodiscard]] bool IsInitialized() const noexcept { return m_Resource.has_value(); }

		LinearMemoryResource& GetResource() noexcept
		{
			ASSERT(IsInitialized(), "Scratch memory must be initialized before use.");
			return *m_Resource;
		}

	private:
		MemoryBlock m_Backing;
		Optional<LinearMemoryResource> m_Resource;
	};
} // namespace rox::core
