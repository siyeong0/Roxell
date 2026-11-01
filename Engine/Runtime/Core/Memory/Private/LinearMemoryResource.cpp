#include <LinearMemoryResource.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Assertion.h>

namespace rox::core
{
	LinearMemoryResource::Marker::Marker(const LinearMemoryResource* const owner, const uint64 serial, const uint64 parentSerial, const usize cursor) noexcept
	    : m_Owner(owner)
	    , m_Serial(serial)
	    , m_ParentSerial(parentSerial)
	    , m_Cursor(cursor)
	{
	}

	LinearMemoryResource::LinearMemoryResource(void* const memory, const usize capacityByteCount)
	    : m_Memory(static_cast<Byte*>(memory))
	    , m_CapacityByteCount(capacityByteCount)
	    , m_UsedByteCount(0)
	    , m_PeakByteCount(0)
	    , m_LastMarkerSerial(0)
	    , m_CurrentMarkerSerial(0)
	{
		ASSERT(m_Memory != nullptr || m_CapacityByteCount == 0,
		       "A non-empty linear memory resource requires a valid backing store.");

		if (m_Memory != nullptr)
		{
			const uintptr baseAddress = reinterpret_cast<uintptr>(m_Memory);
			ASSERT(m_CapacityByteCount <= NumericLimits<uintptr>::max() - baseAddress, "A linear memory backing range must not overflow the address space.");
			ASSERT(m_CapacityByteCount <= static_cast<usize>(NumericLimits<isize>::max()), "A linear memory backing range must support defined pointer arithmetic.");
		}
	}

	LinearMemoryResource::~LinearMemoryResource()
	{
		ASSERT(m_CurrentMarkerSerial == 0, "A linear memory resource must outlive all active markers.");
	}

	LinearMemoryResource::Marker LinearMemoryResource::Mark() noexcept
	{
		ASSERT(m_LastMarkerSerial != NumericLimits<uint64>::max(), "Linear memory marker identities must not overflow.");

		const uint64 serial = ++m_LastMarkerSerial;
		const Marker marker(this, serial, m_CurrentMarkerSerial, m_UsedByteCount);
		m_CurrentMarkerSerial = serial;
		return marker;
	}

	void LinearMemoryResource::Rewind(const Marker marker) noexcept
	{
		ASSERT(marker.m_Owner == this, "A linear memory marker must belong to this resource.");
		ASSERT(marker.m_Serial == m_CurrentMarkerSerial, "Linear memory markers must be consumed once in reverse creation order.");
		ASSERT(marker.m_Cursor <= m_UsedByteCount, "A linear memory marker must not advance the allocation cursor.");

		m_UsedByteCount = marker.m_Cursor;
		m_CurrentMarkerSerial = marker.m_ParentSerial;
	}

	void LinearMemoryResource::Reset() noexcept
	{
		ASSERT(m_CurrentMarkerSerial == 0, "A linear memory reset requires no active markers.");
		m_UsedByteCount = 0;
	}

	usize LinearMemoryResource::GetCapacityByteCount() const noexcept
	{
		return m_CapacityByteCount;
	}

	usize LinearMemoryResource::GetUsedByteCount() const noexcept
	{
		return m_UsedByteCount;
	}

	usize LinearMemoryResource::GetPeakByteCount() const noexcept
	{
		return m_PeakByteCount;
	}

	DiagnosticText LinearMemoryResource::GetName() const noexcept
	{
		return "LinearMemoryResource";
	}

	EMemoryResourceThreadSafety LinearMemoryResource::GetThreadSafety() const noexcept
	{
		return EMemoryResourceThreadSafety::CallerConfined;
	}

	void* LinearMemoryResource::allocate(const MemoryAllocationRequest allocation) noexcept
	{
		ASSERT(m_UsedByteCount <= m_CapacityByteCount, "Linear memory usage must never exceed the resource capacity.");

		const usize availableByteCount = m_CapacityByteCount - m_UsedByteCount;
		const uintptr baseAddress = reinterpret_cast<uintptr>(m_Memory);
		const uintptr currentAddress = baseAddress + m_UsedByteCount;
		const usize alignmentMask = allocation.Alignment - 1;
		const usize paddingByteCount = (allocation.Alignment - (currentAddress & alignmentMask)) & alignmentMask;

		if (paddingByteCount > availableByteCount || allocation.ByteCount > availableByteCount - paddingByteCount)
		{
			return nullptr;
		}

		const usize consumedByteCount = paddingByteCount + allocation.ByteCount;
		Byte* const result = m_Memory + m_UsedByteCount + paddingByteCount;
		m_UsedByteCount += consumedByteCount;
		m_PeakByteCount = core::Max(m_PeakByteCount, m_UsedByteCount);

		return result;
	}

	void LinearMemoryResource::deallocate(void* const memory, const MemoryAllocationRequest allocation) noexcept
	{
		static_cast<void>(memory);
		static_cast<void>(allocation);
	}
} // namespace rox::core
