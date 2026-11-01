#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <RhiCommandTypes.h>
#include <Types.h>
#include <Array.hpp>

#include <d3d12.h>
#include <wrl/client.h>

namespace rox::rhi
{
	// Retain with the attachment until its last submitted use completes.
	class D3D12IntegerClear final
	{
	public:
		// Cold and transactional: failure leaves a previously prepared object unchanged.
		[[nodiscard]] HRESULT Prepare(ID3D12Device& device, DXGI_FORMAT format, core::uint32 sampleCount, EClearColorType type) noexcept;
		[[nodiscard]] bool IsReady() const noexcept;
		[[nodiscard]] bool HasSameProgram(const D3D12IntegerClear& other) const noexcept;

		// The target must already be in RENDER_TARGET state. No allocations or native object creation.
		// Rebind the framebuffer, viewport/scissor, graphics PSO and root arguments before subsequent draws.
		void Record(ID3D12GraphicsCommandList& list, D3D12_CPU_DESCRIPTOR_HANDLE target, core::uint32 width, core::uint32 height, const ClearColor& color) const noexcept;

	private:
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_Pipeline;

		EClearColorType m_Type = EClearColorType::Float;
	};

	// Device-owned immutable programs; captures retain their native owners independently of the cache.
	class D3D12IntegerClearCache final
	{
	public:
		explicit D3D12IntegerClearCache(core::MemoryResource& memory) noexcept;
		[[nodiscard]] HRESULT Capture(ID3D12Device& device, DXGI_FORMAT format, core::uint32 sampleCount, EClearColorType type, D3D12IntegerClear& output) noexcept;
		[[nodiscard]] core::usize GetProgramCount() const noexcept;

	private:
		struct Entry final
		{
			DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
			core::uint32 SampleCount = 0;
			EClearColorType Type = EClearColorType::Float;

			D3D12IntegerClear Program;
		};
		Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
		core::Array<Entry> m_Entries;
	};
}

#endif
