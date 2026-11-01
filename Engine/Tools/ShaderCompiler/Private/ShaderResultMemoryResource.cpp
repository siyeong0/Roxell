#include "ShaderResultMemoryResource.h"

#include <Assertion.h>
#include <StringView.hpp>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	static_assert(sizeof(ShaderResultMemoryResource) + 64 <= RESULT_OWNER_BYTE_CHARGE);

	bool BuildBudgetLedger::TryReserve(uint64 bytes) noexcept
	{
		uint64 used = Used.load(MemoryOrderRelaxed);

		for (;;)
		{
			ASSERT(used <= Limit, "Shader result budget usage must remain within its limit.");
			if (bytes > Limit - used)
			{
				return false;
			}

			if (Used.compare_exchange_weak(used, used + bytes, MemoryOrderAcqRel))
			{
				return true;
			}
		}
	}

	void BuildBudgetLedger::Release(uint64 bytes) noexcept
	{
		const uint64 previous = Used.fetch_sub(bytes, MemoryOrderAcqRel);
		ASSERT(previous >= bytes, "Shader result budget cannot underflow.");
	}

	ShaderResultMemoryResource::ShaderResultMemoryResource(MemoryResource& upstream, SharedPtr<BuildBudgetLedger> ledger, uint64 reservation) noexcept
		: m_Budget(upstream, static_cast<usize>(reservation - RESULT_OWNER_BYTE_CHARGE))
		, m_Ledger(Move(ledger))
		, m_Reservation(reservation)
	{
	}

	ShaderResultMemoryResource::~ShaderResultMemoryResource() noexcept
	{
		ASSERT(GetUsedBytes() == 0, "Shader result allocator requires all allocations released.");
		if (!m_bPublished)
		{
			m_Ledger->Release(m_Reservation);
		}
		else
		{
			m_Ledger->Release(RESULT_OWNER_BYTE_CHARGE);
		}
	}

	void ShaderResultMemoryResource::SetAllocationLease(const SharedPtr<ShaderResultMemoryResource>& owner) noexcept
	{
		ASSERT(owner.Get() == this, "Result allocation lease requires its own allocator owner.");
		m_AllocationLease = owner;
	}

	void ShaderResultMemoryResource::Publish() noexcept
	{
		SharedPtr<ShaderResultMemoryResource> release;
		{
			LockGuard lock(m_Mutex);
			ASSERT(!m_bPublished, "Shader result memory publication is single-shot.");
			const uint64 used = GetUsedBytes();
			m_Ledger->Release(m_Reservation - used - RESULT_OWNER_BYTE_CHARGE);
			m_bPublished = true;
			if (used == 0 && !m_bOwnerPresent)
			{
				release = Move(m_AllocationLease);
			}
		}
	}

	void ShaderResultMemoryResource::ReleaseOwner() noexcept
	{
		SharedPtr<ShaderResultMemoryResource> release;
		{
			LockGuard lock(m_Mutex);
			ASSERT(m_bOwnerPresent, "Result allocator owner releases once.");
			m_bOwnerPresent = false;
			if (GetUsedBytes() == 0)
			{
				release = Move(m_AllocationLease);
			}
		}
	}

	uint64 ShaderResultMemoryResource::GetUsedBytes() const noexcept
	{
		return m_Budget.GetStatistics().UsedByteCount;
	}

	DiagnosticText ShaderResultMemoryResource::GetName() const noexcept
	{
		return "ShaderBuildResult";
	}

	EMemoryResourceThreadSafety ShaderResultMemoryResource::GetThreadSafety() const noexcept
	{
		return EMemoryResourceThreadSafety::ThreadSafe;
	}

	void* ShaderResultMemoryResource::allocate(MemoryAllocationRequest allocation) noexcept
	{
		LockGuard lock(m_Mutex);
		if (m_bPublished && !m_Ledger->TryReserve(allocation.ByteCount))
		{
			return nullptr;
		}

		void* memory = m_Budget.Allocate(allocation);
		if (memory == nullptr && m_bPublished)
		{
			m_Ledger->Release(allocation.ByteCount);
		}

		return memory;
	}

	void ShaderResultMemoryResource::deallocate(void* memory, MemoryAllocationRequest allocation) noexcept
	{
		SharedPtr<ShaderResultMemoryResource> release;
		{
			LockGuard lock(m_Mutex);
			m_Budget.Deallocate(memory, allocation);
			if (m_bPublished)
			{
				m_Ledger->Release(allocation.ByteCount);
			}

			if (GetUsedBytes() == 0 && !m_bOwnerPresent)
			{
				release = Move(m_AllocationLease);
			}
		}
	}
} // namespace rox::shader
