#pragma once

#include <MemoryResource.h>
#include <TextPrimitives.hpp>

namespace rox::core
{
	// Binary-image-local C-heap bootstrap with one backend attempt and no new_handler retry.
	class SystemMemoryResource final : public MemoryResource
	{
	public:
		// Returns an immortal bootstrap resource local to the current executable or DLL image.
		// The instance is constant-initialized, so the accessor is a plain address and every
		// default-constructed String or Error pays no call or guard for it.
		static SystemMemoryResource& Get() noexcept { return s_Instance; }

		DiagnosticText GetName() const noexcept override;
		EMemoryResourceThreadSafety GetThreadSafety() const noexcept override;

	protected:
		void* allocate(MemoryAllocationRequest allocation) noexcept override;
		void deallocate(void* memory, MemoryAllocationRequest allocation) noexcept override;

	private:
		constexpr SystemMemoryResource() = default;

		static SystemMemoryResource s_Instance;
	};
} // namespace rox::core
