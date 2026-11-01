#include "D3D12IntegerClear.h"
#include <Types.h>
#include <Utility.hpp>
#ifdef _WIN32
#include "D3D12IntegerClearShaders.h"
#include <Assertion.h>
#include <Vocabulary.hpp>
namespace rox::rhi
{
	using namespace core;

	HRESULT D3D12IntegerClear::Prepare(ID3D12Device& device, DXGI_FORMAT format, uint32 sampleCount, EClearColorType type) noexcept
	{
		if ((type != EClearColorType::Uint && type != EClearColorType::Sint) || format == DXGI_FORMAT_UNKNOWN || !sampleCount)
		{
			return E_INVALIDARG;
		}

		D3D12_ROOT_PARAMETER constants{};
		constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		constants.Constants.Num32BitValues = 4;
		constants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC rootDesc{};
		rootDesc.NumParameters = 1;
		rootDesc.pParameters = &constants;
		Microsoft::WRL::ComPtr<ID3DBlob> serialized;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		HRESULT result = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), errors.GetAddressOf());
		if (FAILED(result))
		{
			return result;
		}
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
		result = device.CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(root.GetAddressOf()));
		if (FAILED(result))
		{
			return result;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = root.Get();
		desc.VS = { INTEGER_CLEAR_VERTEX, sizeof(INTEGER_CLEAR_VERTEX) };
		desc.PS = type == EClearColorType::Uint ? D3D12_SHADER_BYTECODE{ INTEGER_CLEAR_UNSIGNED, sizeof(INTEGER_CLEAR_UNSIGNED) }
		: D3D12_SHADER_BYTECODE{ INTEGER_CLEAR_SIGNED, sizeof(INTEGER_CLEAR_SIGNED) };
		desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
		desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.DepthClipEnable = TRUE;
		desc.RasterizerState.MultisampleEnable = sampleCount > 1;
		desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		desc.DepthStencilState.FrontFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
											D3D12_COMPARISON_FUNC_ALWAYS };
		desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = format;
		desc.SampleDesc.Count = sampleCount;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
		result = device.CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pipeline.GetAddressOf()));
		if (FAILED(result))
		{
			return result;
		}

		m_RootSignature = Move(root);
		m_Pipeline = Move(pipeline);
		m_Type = type;

		return S_OK;
	}

	D3D12IntegerClearCache::D3D12IntegerClearCache(MemoryResource& memory) noexcept
		: m_Entries(memory)
	{
	}

	HRESULT D3D12IntegerClearCache::Capture(ID3D12Device& device, DXGI_FORMAT format, uint32 sampleCount, EClearColorType type, D3D12IntegerClear& output) noexcept
	{
		if ((m_Device && m_Device.Get() != &device) || format == DXGI_FORMAT_UNKNOWN || !sampleCount || (type != EClearColorType::Uint && type != EClearColorType::Sint))
		{
			return E_INVALIDARG;
		}
		for (rhi::D3D12IntegerClearCache::Entry const& entry : m_Entries)
		{
			if (entry.Format == format && entry.SampleCount == sampleCount && entry.Type == type)
			{
				output = entry.Program;

				return S_OK;
			}
		}
		if (!m_Entries.Reserve(m_Entries.Size() + 1))
		{
			return E_OUTOFMEMORY;
		}
		Entry replacement{ format, sampleCount, type, {} };
		const HRESULT prepared = replacement.Program.Prepare(device, format, sampleCount, type);
		if (FAILED(prepared))
		{
			return prepared;
		}
		const bool bAdded = m_Entries.Add(Move(replacement));
		ASSERT(bAdded, "Integer clear cache storage is reserved before native creation.");
		m_Device = &device;
		output = m_Entries.Back().Program;

		return S_OK;
	}

	usize D3D12IntegerClearCache::GetProgramCount() const noexcept
	{
		return m_Entries.Size();
	}

	bool D3D12IntegerClear::IsReady() const noexcept
	{
		return m_RootSignature.Get() != nullptr && m_Pipeline.Get() != nullptr;
	}

	bool D3D12IntegerClear::HasSameProgram(const D3D12IntegerClear& other) const noexcept
	{
		return IsReady() && other.IsReady() && m_RootSignature.Get() == other.m_RootSignature.Get() && m_Pipeline.Get() == other.m_Pipeline.Get() && m_Type == other.m_Type;
	}

	void D3D12IntegerClear::Record(ID3D12GraphicsCommandList& list, D3D12_CPU_DESCRIPTOR_HANDLE target, uint32 width, uint32 height, const ClearColor& color) const noexcept
	{
		ASSERT(IsReady(), "Integer clears require a prepared native pipeline.");
		ASSERT(color.GetType() == m_Type, "Integer clear values must match the prepared attachment type.");
		ASSERT(width && height, "Integer clear attachment dimensions must be nonzero.");
		const D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
		const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
		list.OMSetRenderTargets(1, &target, FALSE, nullptr);
		list.RSSetViewports(1, &viewport);
		list.RSSetScissorRects(1, &scissor);
		list.SetPipelineState(m_Pipeline.Get());
		list.SetGraphicsRootSignature(m_RootSignature.Get());
		list.SetGraphicsRoot32BitConstants(
			0, 4, m_Type == EClearColorType::Uint ? static_cast<const void*>(color.GetUint()) : static_cast<const void*>(color.GetSint()), 0);
		list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		list.DrawInstanced(3, 1, 0, 0);
	}
}
#endif
