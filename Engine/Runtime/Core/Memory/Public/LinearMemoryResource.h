#pragma once

#include <MemoryResource.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <cstddef>
#include <cstdint>

namespace rox::core
{
	class LinearMemoryResource final : public MemoryResource
	{
	public:
		class Marker final
		{
			friend class LinearMemoryResource;

		public:
			Marker(const Marker&) noexcept = default;
			Marker& operator=(const Marker&) noexcept = default;

		private:
			Marker(const LinearMemoryResource* owner, uint64 serial, uint64 parentSerial, usize cursor) noexcept;

		private:
			const LinearMemoryResource* m_Owner;
			uint64 m_Serial;
			uint64 m_ParentSerial;
			usize m_Cursor;
		};

		LinearMemoryResource(void* memory, usize capacityByteCount);
		~LinearMemoryResource() override;

		[[nodiscard]] Marker Mark() noexcept;
		// Rewind consumes the innermost marker after the caller destroys all subsequent owners.
		void Rewind(Marker marker) noexcept;

		// The owner proves no suballocation is live before resetting the entire borrowed range.
		void Reset() noexcept;

		usize GetCapacityByteCount() const noexcept;
		usize GetUsedByteCount() const noexcept;
		usize GetPeakByteCount() const noexcept;

		DiagnosticText GetName() const noexcept override;
		EMemoryResourceThreadSafety GetThreadSafety() const noexcept override;

	protected:
		void* allocate(MemoryAllocationRequest allocation) noexcept override;
		void deallocate(void* memory, MemoryAllocationRequest allocation) noexcept override;

	private:
		Byte* m_Memory;
		usize m_CapacityByteCount;

		usize m_UsedByteCount;
		usize m_PeakByteCount;

		uint64 m_LastMarkerSerial;
		uint64 m_CurrentMarkerSerial;
	};
} // namespace rox::core
