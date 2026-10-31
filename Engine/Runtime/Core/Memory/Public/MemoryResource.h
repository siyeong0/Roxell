#pragma once

#include <Assertion.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <cstddef>
#include <limits>
#include <new>
#include <string_view>

namespace rox::core
{
#if defined(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
	constexpr rox::core::usize DEFAULT_MEMORY_ALIGNMENT = __STDCPP_DEFAULT_NEW_ALIGNMENT__ > alignof(rox::core::MaxAlign)
	                                                     ? __STDCPP_DEFAULT_NEW_ALIGNMENT__
	                                                     : alignof(rox::core::MaxAlign);
#else
	constexpr rox::core::usize DEFAULT_MEMORY_ALIGNMENT = alignof(rox::core::MaxAlign);
#endif

	struct MemoryAllocationRequest
	{
		rox::core::usize ByteCount = 0;
		rox::core::usize Alignment = DEFAULT_MEMORY_ALIGNMENT;
	};

	enum class EMemoryResourceThreadSafety
	{
		CallerConfined,
		ThreadSafe
	};

	enum class EMemoryFailureReason
	{
		AllocationFailed
	};

	class MemoryResource;

	struct MemoryFailure
	{
		EMemoryFailureReason Reason;
		const MemoryResource* Resource;
		rox::core::DiagnosticText ResourceName;

		MemoryAllocationRequest Allocation;
	};

	using MemoryFailureHandler = void (*)(const MemoryFailure& failure) noexcept;

	constexpr bool IsValidMemoryAlignment(const rox::core::usize alignment) noexcept
	{
		return alignment != 0 && (alignment & (alignment - 1)) == 0;
	}

	constexpr bool AddMemorySize(const rox::core::usize left, const rox::core::usize right, rox::core::usize* const outResult) noexcept
	{
		ASSERT(outResult != nullptr, "AddMemorySize requires a valid output pointer.");

		if (left > rox::core::NumericLimits<rox::core::usize>::max() - right)
		{
			return false;
		}

		*outResult = left + right;
		return true;
	}

	constexpr bool MultiplyMemorySize(const rox::core::usize left, const rox::core::usize right, rox::core::usize* const outResult) noexcept
	{
		ASSERT(outResult != nullptr, "MultiplyMemorySize requires a valid output pointer.");

		if (left != 0 && right > rox::core::NumericLimits<rox::core::usize>::max() / left)
		{
			return false;
		}

		*outResult = left * right;
		return true;
	}

	// The handler is binary-image-local, allocation-free, and may be entered concurrently by independent threads.
	MemoryFailureHandler SetMemoryFailureHandler(MemoryFailureHandler handler) noexcept;

	[[noreturn]] void ReportMemoryFailure(const MemoryFailure& failure) noexcept;

	// PMR-style allocation policy base. Every resource must outlive all allocations it owns.
	class MemoryResource
	{
	public:
		virtual ~MemoryResource() = default;

		MemoryResource(const MemoryResource&) = delete;
		MemoryResource& operator=(const MemoryResource&) = delete;
		MemoryResource(MemoryResource&&) = delete;
		MemoryResource& operator=(MemoryResource&&) = delete;

		// Allocate returns nullptr for zero bytes or exhaustion. Invalid requests are programmer errors.
		[[nodiscard]] void* Allocate(MemoryAllocationRequest allocation) noexcept;

		// Use only when the caller cannot recover from exhaustion; reports failure and terminates.
		void* AllocateRequired(MemoryAllocationRequest allocation) noexcept;

		// The resource, byte count, and alignment must match the allocation request.
		void Deallocate(void* memory, MemoryAllocationRequest allocation) noexcept;

		// The returned view must have static storage duration.
		virtual rox::core::DiagnosticText GetName() const noexcept = 0;
		virtual EMemoryResourceThreadSafety GetThreadSafety() const noexcept = 0;

	protected:
		MemoryResource() = default;

		virtual void* allocate(MemoryAllocationRequest allocation) noexcept = 0;
		virtual void deallocate(void* memory, MemoryAllocationRequest allocation) noexcept = 0;
	};
} // namespace rox::core
