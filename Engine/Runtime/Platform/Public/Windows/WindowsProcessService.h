#pragma once

#include <Cancellation.h>

#include <Hash.h>
#include <IProcessService.h>

namespace rox::os
{
	inline constexpr core::uint64 MAXIMUM_PROCESS_IMAGE_HASH_BYTES = 512ull * 1024ull * 1024ull;

	// SHA-256 of the current executable file, including non-code bytes. Does not hash DLLs.
	// Synchronous, bounded, read-only OS mapping; every call closes its handles and view.
	// No path, timestamp or relocated in-memory address is part of the hash.
	[[nodiscard]] core::Result<core::Hash256> ReadCurrentWindowsProcessImageHash(core::uint64 maximumBytes = MAXIMUM_PROCESS_IMAGE_HASH_BYTES);

	struct WindowsProcessServiceOptions final
	{
		core::uint32 MaximumProcesses = DEFAULT_MAXIMUM_PROCESS_COUNT;
	};

	class WindowsProcessService final : public IProcessService
	{
	public:
		WindowsProcessService() noexcept = default;
		~WindowsProcessService() noexcept override;
		WindowsProcessService(const WindowsProcessService&) = delete;
		WindowsProcessService& operator=(const WindowsProcessService&) = delete;
		WindowsProcessService(WindowsProcessService&& other) noexcept;
		WindowsProcessService& operator=(WindowsProcessService&& other) noexcept;

		[[nodiscard]] static core::Result<void> TryCreate(core::MemoryResource& resource, WindowsProcessServiceOptions options, WindowsProcessService* outService);

		[[nodiscard]] core::Task<ProcessHandle> Launch(const ProcessLaunchDesc& desc, core::CancellationToken cancellation) override;

		[[nodiscard]] core::Task<ProcessExit> Wait(ProcessHandle process, core::MemoryResource& resultResource, core::CancellationToken cancellation) override;

		[[nodiscard]] core::Result<void> RequestTermination(ProcessHandle process) override;
		[[nodiscard]] core::Result<void> Release(ProcessHandle process) override;

		[[nodiscard]] bool IsValid() const noexcept;

	private:
		class State;

		WindowsProcessService(core::MemoryResource& resource, State* state) noexcept;
		void assertInvariant() const noexcept;

	private:
		core::MemoryResource* m_Resource = nullptr;
		State* m_State = nullptr;
	};
} // namespace rox::os
