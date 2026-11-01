#include <SystemMemoryResource.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Assertion.h>

#include <cstdlib>
#include <memory>
#include <new>

namespace rox::core
{
	namespace
	{
		struct SystemAllocationHeader final
		{
			SystemMemoryResource* Owner;
			void* RawMemory;
			MemoryAllocationRequest RequestedAllocation;
		};
	}

	constinit SystemMemoryResource SystemMemoryResource::s_Instance;

	DiagnosticText SystemMemoryResource::GetName() const noexcept
	{
		return "SystemMemoryResource";
	}

	EMemoryResourceThreadSafety SystemMemoryResource::GetThreadSafety() const noexcept
	{
		return EMemoryResourceThreadSafety::ThreadSafe;
	}

	void* SystemMemoryResource::allocate(const MemoryAllocationRequest allocation) noexcept
	{
		const usize effectiveAlignment = core::Max(allocation.Alignment, alignof(SystemAllocationHeader));
		usize allocationOverhead = 0;

		if (!AddMemorySize(sizeof(SystemAllocationHeader), effectiveAlignment - 1, &allocationOverhead))
		{
			return nullptr;
		}

		usize totalByteCount = 0;

		if (!AddMemorySize(allocationOverhead, allocation.ByteCount, &totalByteCount) ||
		    totalByteCount > static_cast<usize>(NumericLimits<isize>::max()))
		{
			return nullptr;
		}

		void* const rawMemory = std::malloc(totalByteCount);

		if (rawMemory == nullptr)
		{
			return nullptr;
		}

		void* candidateMemory = static_cast<Byte*>(rawMemory) + sizeof(SystemAllocationHeader);
		usize availableByteCount = totalByteCount - sizeof(SystemAllocationHeader);
		void* const alignedMemory = std::align(effectiveAlignment, allocation.ByteCount, candidateMemory, availableByteCount);
		ASSERT(alignedMemory != nullptr, "System allocation overhead must provide enough storage for the requested alignment.");

		SystemAllocationHeader* const header = reinterpret_cast<SystemAllocationHeader*>(static_cast<Byte*>(alignedMemory) - sizeof(SystemAllocationHeader));
		::new (header) SystemAllocationHeader{this, rawMemory, allocation};
		return alignedMemory;
	}

	void SystemMemoryResource::deallocate(void* const memory, const MemoryAllocationRequest allocation) noexcept
	{
		SystemAllocationHeader* const header = reinterpret_cast<SystemAllocationHeader*>(static_cast<Byte*>(memory) - sizeof(SystemAllocationHeader));
		ASSERT(header->Owner == this, "Storage must be deallocated by the resource that allocated it.");
		ASSERT(header->RequestedAllocation.ByteCount == allocation.ByteCount && header->RequestedAllocation.Alignment == allocation.Alignment, "Deallocation metadata must match the original system allocation request.");

		void* const rawMemory = header->RawMemory;
		header->~SystemAllocationHeader();
		std::free(rawMemory);
	}
} // namespace rox::core
