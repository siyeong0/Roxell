#include <RhiInterop.h>

#include <Assertion.h>
#include <Log.h>
#include <Utility.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif
namespace rox::rhi
{
	using namespace core;

	ExternalHandle ExternalHandle::Adopt(EExternalHandleType type, uint64 value) noexcept
	{
		ASSERT((ExternalHandleView{ type, value }.IsValid()), "Adoption requires an owned valid external OS handle.");
		return ExternalHandle(type, value);
	}

	ExternalBufferMemoryView ExternalBufferMemory::Borrow() const noexcept
	{
		return { Descriptor, Handle.Borrow(), Device, AllocationBytes, MemoryTypeIndex, bDedicated };
	}

	ExternalTextureMemoryView ExternalTextureMemory::Borrow() const noexcept
	{
		return { Descriptor, Handle.Borrow(), Device, AllocationBytes, MemoryTypeIndex, bDedicated };
	}

	ExternalHandle::~ExternalHandle()
	{
		if (IsValid())
		{
			const Result<void> closed = Close ();
			if (!closed)
			{
				LOG_ERROR("Rhi.ExternalHandle", closed.GetError().Message.View());
			}
		}
	}

	ExternalHandle::ExternalHandle(ExternalHandle&& other) noexcept
		: m_Handle(other.m_Handle)
	{
		other.m_Handle = {};
	}

	ExternalHandle& ExternalHandle::operator=(ExternalHandle&& other) noexcept
	{
		if (this != &other)
		{
			ExternalHandle previous(Move(*this));
			m_Handle = other.m_Handle;
			other.m_Handle = {};
		}

		return *this;
	}

	ExternalHandleView ExternalHandle::Borrow() const noexcept
	{
		return m_Handle;
	}

	bool ExternalHandle::IsValid() const noexcept
	{
		return m_Handle.IsValid();
	}

	Result<void> ExternalHandle::Close() noexcept
	{
		ASSERT(IsValid(), "Closing an external OS handle requires a valid owner.");
#ifdef _WIN32
		if (m_Handle.Type == EExternalHandleType::OpaqueFd)
#else
		if (m_Handle.Type != EExternalHandleType::OpaqueFd)
#endif
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ExternalHandle", "The external OS handle type is unsupported on this platform.");
		}
#ifdef _WIN32
		const bool bClosed = CloseHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(m_Handle.Value))) != 0;
		if (bClosed)
		{
			m_Handle = {};
		}
#else
		const bool bClosed = close(static_cast<int>(m_Handle.Value)) == 0;
		// POSIX close failures can leave the descriptor closed, so retrying can close a reused descriptor.
		m_Handle = {};
#endif
		if (!bClosed)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.ExternalHandle", "Could not close the external OS handle.");
		}

		return Result<void>::Success();
	}

	ExternalHandle::ExternalHandle(EExternalHandleType type, uint64 value) noexcept
		: m_Handle{ type, value }
	{
	}
}

namespace rox::rhi
{
	using namespace core;

	NativeBufferState NativeBufferState::FromNative(EBackend backend, uint32 state) noexcept
	{
		ASSERT(backend == EBackend::Null || backend == EBackend::D3D12 || backend == EBackend::Vulkan, "Native state requires a supported backend.");
		ASSERT(backend == EBackend::D3D12 || state == 0, "Only D3D12 buffers carry a native resource-state payload.");
		return NativeBufferState(backend, state);
	}

	EBackend NativeBufferState::GetBackend() const noexcept
	{
		return m_Backend;
	}

	uint32 NativeBufferState::GetD3D12ResourceStates() const noexcept
	{
		ASSERT(m_Backend == EBackend::D3D12, "D3D12 resource states require a D3D12 buffer state.");
		return m_ResourceStates;
	}

	NativeBufferState::NativeBufferState(const EBackend backend, const uint32 resourceStates) noexcept
		: m_Backend(backend)
		, m_ResourceStates(resourceStates)
	{
	}

	NativeTextureState NativeTextureState::FromNative(EBackend backend, uint32 state) noexcept
	{
		ASSERT(backend == EBackend::Null || backend == EBackend::D3D12 || backend == EBackend::Vulkan, "Native state requires a supported backend.");
		return NativeTextureState(backend, state);
	}

	EBackend NativeTextureState::GetBackend() const noexcept
	{
		return m_Backend;
	}

	uint32 NativeTextureState::GetD3D12ResourceStates() const noexcept
	{
		ASSERT(m_Backend == EBackend::D3D12, "D3D12 resource states require a D3D12 texture state.");
		return m_State;
	}

	uint32 NativeTextureState::GetVulkanImageLayout() const noexcept
	{
		ASSERT(m_Backend == EBackend::Vulkan, "A Vulkan image layout requires a Vulkan texture state.");
		return m_State;
	}

	NativeTextureState::NativeTextureState(const EBackend backend, const uint32 state) noexcept
		: m_Backend(backend)
		, m_State(state)
	{
	}
}
