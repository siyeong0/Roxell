#pragma once

#include <Result.hpp>
#include <RhiResourceTypes.h>
#include <RhiTypes.h>

namespace rox::rhi
{
	class NativeBufferState final
	{
	public:
		[[nodiscard]] static NativeBufferState FromNative(EBackend backend, core::uint32 state) noexcept;
		[[nodiscard]] EBackend GetBackend() const noexcept;
		[[nodiscard]] core::uint32 GetD3D12ResourceStates() const noexcept;

	private:
		NativeBufferState(EBackend backend, core::uint32 resourceStates) noexcept;

	private:
		EBackend m_Backend;
		core::uint32 m_ResourceStates;
	};

	class NativeTextureState final
	{
	public:
		[[nodiscard]] static NativeTextureState FromNative(EBackend backend, core::uint32 state) noexcept;
		[[nodiscard]] EBackend GetBackend() const noexcept;
		[[nodiscard]] core::uint32 GetD3D12ResourceStates() const noexcept;
		[[nodiscard]] core::uint32 GetVulkanImageLayout() const noexcept;

	private:
		NativeTextureState(EBackend backend, core::uint32 state) noexcept;

	private:
		EBackend m_Backend;
		core::uint32 m_State;
	};

	enum class ENativeTextureContents : core::uint8
	{
		Discard,
		Preserve
	};

	struct NativeDeviceHandles final
	{
		EBackend Backend = EBackend::Null;

		void* Device = nullptr;
		void* Queue = nullptr;

		void* Instance = nullptr;
		void* PhysicalDevice = nullptr;

		core::uint32 QueueFamilyIndex = 0xFFFFFFFFu;

		core::uint32 ApiVersion = 0;
	};

	struct NativeResourceHandle final
	{
		EBackend Backend = EBackend::Null;
		void* Handle = nullptr;
	};

	struct NativeQueueHandle final
	{
		EBackend Backend = EBackend::Null;
		void* Handle = nullptr;
		core::uint32 QueueFamilyIndex = 0xFFFFFFFFu;
	};

	struct NativeCommandListHandle final
	{
		EBackend Backend = EBackend::Null;
		void* Handle = nullptr;
	};
	// Advanced native queue handoff and sparse mapping retirement facts.
	enum class ESubmissionDisposition : core::uint8
	{
		NotAccepted,
		Accepted,
		Indeterminate
	};

	enum class ESubmissionInputState : core::uint8
	{
		Retained,
		Consumed
	};

	struct NativeSubmissionOutcome final
	{
		ESubmissionDisposition Disposition = ESubmissionDisposition::NotAccepted;
		// For command lists, Retained preserves caller ownership but does not guarantee retry.
		// Mapping handles have a separate lifecycle.
		ESubmissionInputState InputState = ESubmissionInputState::Retained;

		SubmissionToken Token;

		core::EErrorCode ErrorCode = core::EErrorCode::None;
		core::uint32 Diagnostic = 0;

		core::uint32 QueueWaitCount = 0;
	};

	enum class EExternalHandleType : core::uint8
	{
		None,
		OpaqueWin32,
		OpaqueFd,
		D3D12Resource,
		D3D12Fence
	};

	struct GpuIdentity final
	{
		core::uint8 DeviceUuid[16]{};
		core::uint8 DeviceLuid[8]{};
		core::uint32 NodeMask = 0;

		bool bUuidValid = false;
		bool bLuidValid = false;
	};

	// Import borrows this value; its owner must remain alive during the import call.
	struct ExternalHandleView final
	{
		EExternalHandleType Type = EExternalHandleType::None;
		core::uint64 Value = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return Type > EExternalHandleType::None && Type <= EExternalHandleType::D3D12Fence && (Type == EExternalHandleType::OpaqueFd ? Value <= 0x7FFFFFFFu : Value != 0 && Value != UINT64_MAX);
		}
	};

	class ExternalHandle final
	{
	public:
		ExternalHandle() noexcept = default;
		// Takes an already owned OS handle without duplicating it. File descriptor zero is valid.
		[[nodiscard]] static ExternalHandle Adopt(EExternalHandleType type, core::uint64 value) noexcept;
		~ExternalHandle();
		ExternalHandle(const ExternalHandle&) = delete;
		ExternalHandle& operator=(const ExternalHandle&) = delete;
		ExternalHandle(ExternalHandle&& other) noexcept;
		ExternalHandle& operator=(ExternalHandle&& other) noexcept;

		[[nodiscard]] ExternalHandleView Borrow() const noexcept;
		[[nodiscard]] bool IsValid() const noexcept;
		// Requires a valid owner; OS close failure is returned and POSIX attempts consume the owner.
		[[nodiscard]] core::Result<void> Close() noexcept;

	private:
		ExternalHandle(EExternalHandleType type, core::uint64 value) noexcept;

	private:
		ExternalHandleView m_Handle;
	};

	struct ExternalBufferMemoryView final
	{
		BufferDesc Descriptor;

		ExternalHandleView Handle;
		GpuIdentity Device;

		core::uint64 AllocationBytes = 0;
		core::uint32 MemoryTypeIndex = 0;
		bool bDedicated = true;
	};

	struct ExternalBufferMemory final
	{
		BufferDesc Descriptor;

		ExternalHandle Handle;
		GpuIdentity Device;

		core::uint64 AllocationBytes = 0;
		core::uint32 MemoryTypeIndex = 0;
		bool bDedicated = true;

		[[nodiscard]] ExternalBufferMemoryView Borrow() const noexcept;
	};

	struct ExternalTextureMemoryView final
	{
		TextureDesc Descriptor;

		ExternalHandleView Handle;
		GpuIdentity Device;

		core::uint64 AllocationBytes = 0;
		core::uint32 MemoryTypeIndex = 0;
		bool bDedicated = true;
	};

	struct ExternalTextureMemory final
	{
		TextureDesc Descriptor;

		ExternalHandle Handle;
		GpuIdentity Device;

		core::uint64 AllocationBytes = 0;
		core::uint32 MemoryTypeIndex = 0;
		bool bDedicated = true;

		[[nodiscard]] ExternalTextureMemoryView Borrow() const noexcept;
	};

	struct ExternalFenceView final
	{
		ExternalHandleView Handle;
		GpuIdentity Device;
	};

	struct ExternalFence final
	{
		ExternalHandle Handle;
		GpuIdentity Device;
		[[nodiscard]] ExternalFenceView Borrow() const noexcept { return { Handle.Borrow(), Device }; }
	};

	struct ExternalFenceHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		auto operator<=>(const ExternalFenceHandle&) const = default;
	};

	struct ExternalInteropCapabilities final
	{
		GpuIdentity Device;

		EExternalHandleType MemoryHandleType = EExternalHandleType::None;
		EExternalHandleType FenceHandleType = EExternalHandleType::None;

		bool bBuffers = false;
		bool bTextures = false;
		bool bTimelineFences = false;

		// Diagnostic identities for reproducible evaluation; they do not affect external-memory matching.
		core::uint8 DriverBuildUuid[16]{};
		core::uint64 AdapterDriverVersion = 0;

		bool bDriverBuildUuidValid = false;
		bool bAdapterDriverVersionValid = false;
	};
}
