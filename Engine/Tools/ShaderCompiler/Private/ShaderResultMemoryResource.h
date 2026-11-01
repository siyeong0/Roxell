#pragma once

#include <BudgetMemoryResource.h>
#include <SharedPtr.hpp>
#include <StringView.hpp>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>

namespace rox::shader
{
	inline constexpr core::uint64 RESULT_OWNER_BYTE_CHARGE = 1024;

	struct BuildBudgetLedger
	{
		explicit BuildBudgetLedger(core::uint64 limit) noexcept
			: Limit(limit)
		{
		}

		bool TryReserve(core::uint64 bytes) noexcept;
		void Release(core::uint64 bytes) noexcept;
		const core::uint64 Limit;
		core::Atomic<core::uint64> Used{0};
	};

	// Live allocations retain this allocator independently of the immutable result owner.
	class ShaderResultMemoryResource final : public core::MemoryResource
	{
	public:
		ShaderResultMemoryResource(core::MemoryResource&, core::SharedPtr<BuildBudgetLedger>, core::uint64) noexcept;
		~ShaderResultMemoryResource() noexcept override;
		void SetAllocationLease(const core::SharedPtr<ShaderResultMemoryResource>&) noexcept;
		void Publish() noexcept;
		void ReleaseOwner() noexcept;
		core::uint64 GetUsedBytes() const noexcept;
		core::DiagnosticText GetName() const noexcept override;
		core::EMemoryResourceThreadSafety GetThreadSafety() const noexcept override;

	protected:
		void* allocate(core::MemoryAllocationRequest) noexcept override;
		void deallocate(void*, core::MemoryAllocationRequest) noexcept override;

	private:
		core::BudgetMemoryResource m_Budget;
		core::SharedPtr<BuildBudgetLedger> m_Ledger;
		core::SharedPtr<ShaderResultMemoryResource> m_AllocationLease;
		core::Mutex m_Mutex;
		core::uint64 m_Reservation;
		bool m_bPublished = false;
		bool m_bOwnerPresent = true;
	};

} // namespace rox::shader
