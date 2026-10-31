#include <MemoryBlock.h>
#include <Types.h>
#include <Utility.hpp>
#include <Assertion.h>

namespace rox::core
{
	MemoryBlock::MemoryBlock() noexcept
	    : m_Resource(nullptr)
	    , m_Memory(nullptr)
	    , m_Allocation()
	{
	}

	MemoryBlock::~MemoryBlock()
	{
		Reset();
	}

	MemoryBlock::MemoryBlock(MemoryBlock&& other) noexcept
	    : MemoryBlock()
	{
		moveFrom(rox::core::Move(other));
	}

	MemoryBlock& MemoryBlock::operator=(MemoryBlock&& other) noexcept
	{
		if (this != &other)
		{
			// Publish the replacement before releasing the old block: release callbacks may reset this owner.
			MemoryBlock previous(rox::core::Move(*this));
			moveFrom(rox::core::Move(other));
		}

		return *this;
	}

	MemoryBlock MemoryBlock::Create(MemoryResource& resource, MemoryAllocationRequest allocation) noexcept
	{
		MemoryBlock block;
		(void)CreateInto(resource, &block, allocation);
		return block;
	}

	bool MemoryBlock::CreateInto(MemoryResource& resource, MemoryBlock* const outBlock, const MemoryAllocationRequest allocation) noexcept
	{
		ASSERT(outBlock != nullptr, "MemoryBlock::CreateInto requires a valid output pointer.");

		void* const memory = resource.Allocate(allocation);

		if (memory == nullptr && allocation.ByteCount != 0)
		{
			return false;
		}

		MemoryBlock replacement(resource, memory, allocation);
		*outBlock = rox::core::Move(replacement);
		return true;
	}

	MemoryBlock MemoryBlock::CreateRequired(MemoryResource& resource, const MemoryAllocationRequest allocation) noexcept
	{
		return MemoryBlock(resource, resource.AllocateRequired(allocation), allocation);
	}

	void MemoryBlock::Reset() noexcept
	{
		ASSERT((m_Resource == nullptr) == (m_Memory == nullptr), "A memory block must keep its resource and storage ownership paired.");

		MemoryResource* const resource = rox::core::Exchange(m_Resource, nullptr);
		void* const memory = rox::core::Exchange(m_Memory, nullptr);
		const MemoryAllocationRequest allocation = rox::core::Exchange(m_Allocation, MemoryAllocationRequest{});
		if (resource != nullptr)
		{
			ASSERT(allocation.ByteCount != 0, "An owned memory block must have a non-zero byte count.");
			resource->Deallocate(memory, allocation);
		}
	}

	usize MemoryBlock::GetByteCount() const noexcept
	{
		return m_Allocation.ByteCount;
	}

	usize MemoryBlock::GetAlignment() const noexcept
	{
		return m_Allocation.Alignment;
	}

	MemoryResource* MemoryBlock::GetResource() const noexcept
	{
		return m_Resource;
	}

	bool MemoryBlock::IsEmpty() const noexcept
	{
		return m_Memory == nullptr;
	}

	MemoryBlock::MemoryBlock(MemoryResource& resource, void* const memory, const MemoryAllocationRequest allocation) noexcept
	    : m_Resource(memory == nullptr ? nullptr : &resource)
	    , m_Memory(memory)
	    , m_Allocation(memory == nullptr ? MemoryAllocationRequest{} : allocation)
	{
		ASSERT(memory != nullptr || allocation.ByteCount == 0, "A failed non-empty allocation cannot be stored in a memory block.");
	}

	void MemoryBlock::moveFrom(MemoryBlock&& other) noexcept
	{
		ASSERT((other.m_Resource == nullptr) == (other.m_Memory == nullptr), "A source memory block must have paired resource and storage ownership.");

		m_Resource = other.m_Resource;
		m_Memory = other.m_Memory;
		m_Allocation = other.m_Allocation;

		other.m_Resource = nullptr;
		other.m_Memory = nullptr;
		other.m_Allocation = {};
		ASSERT((m_Resource == nullptr) == (m_Memory == nullptr), "A moved memory block must preserve paired resource and storage ownership.");
	}
} // namespace rox::core
