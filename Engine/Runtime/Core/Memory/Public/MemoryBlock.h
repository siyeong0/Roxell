#pragma once

#include <MemoryResource.h>
#include <Types.h>
#include <cstddef>

namespace rox::core
{
	class MemoryBlock final
	{
	public:
		// The resource returned by GetResource must outlive this block.
		MemoryBlock() noexcept;
		~MemoryBlock();

		MemoryBlock(MemoryBlock&& other) noexcept;
		MemoryBlock& operator=(MemoryBlock&& other) noexcept;

		MemoryBlock(const MemoryBlock&) = delete;
		MemoryBlock& operator=(const MemoryBlock&) = delete;

		[[nodiscard]] static MemoryBlock Create(MemoryResource& resource, MemoryAllocationRequest allocation) noexcept;
		[[nodiscard]] static bool CreateInto(MemoryResource& resource, MemoryBlock* outBlock, MemoryAllocationRequest allocation) noexcept;
		static MemoryBlock CreateRequired(MemoryResource& resource, MemoryAllocationRequest allocation) noexcept;

		// Detach before release callbacks. Reentrant Reset is safe; callback-installed ownership is retained.
		void Reset() noexcept;

		void* GetData() noexcept { return m_Memory; }
		const void* GetData() const noexcept { return m_Memory; }
		usize GetByteCount() const noexcept;
		usize GetAlignment() const noexcept;
		MemoryResource* GetResource() const noexcept;
		bool IsEmpty() const noexcept;

	private:
		MemoryBlock(MemoryResource& resource, void* memory, MemoryAllocationRequest allocation) noexcept;
		void moveFrom(MemoryBlock&& other) noexcept;

	private:
		MemoryResource* m_Resource;
		void* m_Memory;
		MemoryAllocationRequest m_Allocation;
	};
} // namespace rox::core
