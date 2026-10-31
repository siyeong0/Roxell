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
			Marker(const LinearMemoryResource* owner, rox::core::uint64 serial, rox::core::uint64 parentSerial, rox::core::usize cursor) noexcept;

		private:
			const LinearMemoryResource* m_Owner;
			rox::core::uint64 m_Serial;
			rox::core::uint64 m_ParentSerial;
			rox::core::usize m_Cursor;
		};

		LinearMemoryResource(void* memory, rox::core::usize capacityByteCount);
		~LinearMemoryResource() override;

		[[nodiscard]] Marker Mark() noexcept;
		// Rewind consumes the innermost marker after the caller destroys all subsequent owners.
		void Rewind(Marker marker) noexcept;

		// The owner proves no suballocation is live before resetting the entire borrowed range.
		void Reset() noexcept;

		rox::core::usize GetCapacityByteCount() const noexcept;
		rox::core::usize GetUsedByteCount() const noexcept;
		rox::core::usize GetPeakByteCount() const noexcept;

		rox::core::DiagnosticText GetName() const noexcept override;
		EMemoryResourceThreadSafety GetThreadSafety() const noexcept override;

	protected:
		void* allocate(MemoryAllocationRequest allocation) noexcept override;
		void deallocate(void* memory, MemoryAllocationRequest allocation) noexcept override;

	private:
		rox::core::Byte* m_Memory;
		rox::core::usize m_CapacityByteCount;

		rox::core::usize m_UsedByteCount;
		rox::core::usize m_PeakByteCount;

		rox::core::uint64 m_LastMarkerSerial;
		rox::core::uint64 m_CurrentMarkerSerial;
	};
} // namespace rox::core
