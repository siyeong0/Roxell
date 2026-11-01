#pragma once
#ifdef _WIN32
#include "D3D12Backend.h"
namespace rox::d3d12
{
	inline constexpr core::uint32 GLOBAL_RESOURCE_DESCRIPTOR_COUNT = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2;
	inline constexpr core::uint32 GLOBAL_SAMPLER_DESCRIPTOR_COUNT = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;

	[[nodiscard]] bool InitializeGlobalDescriptorHeaps(State& state);
	[[nodiscard]] bool EnsureGlobalDescriptorHeaps(State& state);

	[[nodiscard]] core::Result<core::uint32> AllocateGlobalDescriptors(State& state, bool bSampler, core::uint32 count);
	void ReleaseGlobalDescriptors(State& state, bool bSampler, core::uint32 first, core::uint32 count) noexcept;

	[[nodiscard]] core::Result<core::uint32> AllocatePendingGlobalDescriptors(State& state, Pending& pending, bool bSampler, core::uint32 count);
	void ReleasePendingGlobalDescriptors(State& state, Pending& pending) noexcept;

	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GlobalDescriptorCpu(const State& state, bool bSampler, core::uint32 index) noexcept;
	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GlobalDescriptorGpu(const State& state, bool bSampler, core::uint32 index) noexcept;

	[[nodiscard]] core::Result<void*> CreateBufferView(State* opaque, void* native, const BufferViewDesc& desc);
	void DestroyBufferView(State* opaque, void* native) noexcept;

	template <class TBinding> [[nodiscard]] core::uint32 CountTypedBuffers(core::Span<const TBinding> buffers) noexcept
	{
		core::uint32 count = 0;

		for (const TBinding& binding : buffers)
		{
			count += binding.View != nullptr ? 1u : 0u;
		}

		return count;
	}

	template <class TBinding>
	void CopyTypedBufferDescriptors(State& state, Pending& pending, core::Span<const TBinding> buffers, D3D12_CPU_DESCRIPTOR_HANDLE destination) noexcept
	{
		const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		pending.TypedBufferStride = stride;

		for (const TBinding& binding : buffers)
		{
			if (binding.View == nullptr)
			{
				continue;
			}
			CopyCommandDescriptors(state, pending, 1, destination,
				static_cast<const BufferView*>(binding.View)->Descriptor->GetCPUDescriptorHandleForHeapStart(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			destination.ptr += stride;
		}
	}

	template <class TBinding>
	[[nodiscard]] core::Result<void> PrepareGlobalTypedBuffers(State& state, Pending& pending, core::Span<const TBinding> buffers)
	{
		const core::uint32 count = CountTypedBuffers(buffers);

		if (count == 0)
		{
			return core::Result<void>::Success();
		}
		bool bReuse = pending.LastGlobalTypedCount == buffers.Size() && pending.LastGlobalTypedGpu.ptr != 0;

		for (core::uint32 index = 0; bReuse && index < buffers.Size(); ++index)
		{
			bReuse = pending.LastGlobalTypedViews[index] == buffers[index].View;
		}

		if (bReuse)
		{
			pending.TypedBufferGpu = pending.LastGlobalTypedGpu;

			return core::Result<void>::Success();
		}

		core::Result<core::uint32> range = AllocatePendingGlobalDescriptors(state, pending, false, count);

		if (!range)
		{
			return core::Result<void>::Failure(range.GetError());
		}
		CopyTypedBufferDescriptors(state, pending, buffers, GlobalDescriptorCpu(state, false, range.Value()));
		pending.TypedBufferGpu = GlobalDescriptorGpu(state, false, range.Value());
		pending.LastGlobalTypedGpu = pending.TypedBufferGpu;
		pending.LastGlobalTypedCount = static_cast<core::uint32>(buffers.Size());

		for (core::uint32 index = 0; index < buffers.Size(); ++index)
		{
			pending.LastGlobalTypedViews[index] = buffers[index].View;
		}

		return core::Result<void>::Success();
	}

	template <class TBinding>
	void BindTypedBufferRoots(Pending& pending, core::Span<const TBinding> buffers, core::uint32 rootOffset, bool bGraphics) noexcept
	{
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = pending.TypedBufferGpu;

		for (core::uint32 index = 0; index < buffers.Size(); ++index)
		{
			if (buffers[index].View == nullptr)
			{
				continue;
			}

			if (bGraphics)
			{
				pending.List->SetGraphicsRootDescriptorTable(rootOffset + index, gpu);
			}
			else
			{
				pending.List->SetComputeRootDescriptorTable(rootOffset + index, gpu);
			}
			gpu.ptr += pending.TypedBufferStride;
		}
	}

	[[nodiscard]] core::uint64 GetDescriptorAccelerationStructure(const void* native) noexcept;
	[[nodiscard]] core::Result<void*> CreateDescriptorTable(State* opaque, const DescriptorTableDesc& desc);
	void PublishTableDescriptor(State& state, const DescriptorTable& table, core::uint32 index) noexcept;
	[[nodiscard]] core::Result<void> WriteDescriptorTable(
		State* opaque,
		void* native,
		core::uint32 index,
		void* textureNative,
		void* viewNative,
		void* samplerNative,
		void* bufferNative,
		BufferRange range);
	[[nodiscard]] core::Result<void> CopyDescriptorTableEntries(
		State* opaque,
		const void* sourceNative,
		core::uint32 sourceIndex,
		void* destinationNative,
		core::uint32 destinationIndex,
		core::uint32 count);
	void DestroyDescriptorTable(State* opaque, void* native) noexcept;
	[[nodiscard]] core::uint32 GetDescriptorTableDeviceHeapIndex(const State* opaque, const void* native) noexcept;

	template <typename TLayout> [[nodiscard]] core::uint32 RangeRootCost(core::Span<const TLayout> layouts) noexcept
	{
		core::uint32 count = 0;
		for (const TLayout& layout : layouts)
		{
			count += static_cast<core::uint32>(layout.Ranges.Size());
		}

		return count;
	}

	template <typename TLayout> [[nodiscard]] core::uint32 RangeRootDwordCost(core::Span<const TLayout> layouts) noexcept
	{
		core::uint32 count = 0;
		for (const TLayout& layout : layouts)
		{
			for (core::uint32 index = 0; index < layout.Ranges.Size(); ++index)
			{
				count += layout.Range(index).BufferMode == EBufferBindingMode::RootDescriptor ? 2u : 1u;
			}
		}
		return count;
	}

	template <typename TLayout>
	void FillRangeRootParameters(
		NativeFixedLayoutState& fixed,
		core::Span<const TLayout> layouts,
		D3D12_ROOT_PARAMETER* parameters,
		D3D12_DESCRIPTOR_RANGE* ranges,
		core::uint32 rootOffset,
		bool bGraphics) noexcept
	{
		fixed.LayoutCount = static_cast<core::uint32>(layouts.Size());
		fixed.bPreparedFixedCacheEligible = true;
		fixed.bHasStorage = false;
		core::uint32 index = 0;
		for (core::uint32 layout = 0; layout < layouts.Size(); ++layout)
		{
			fixed.Spaces[layout] = layouts[layout].Space;
			fixed.FixedDescriptorCounts[layout] = 0;
			fixed.RangeCounts[layout] = static_cast<core::uint32>(layouts[layout].Ranges.Size());
			for (core::uint32 rangeIndex = 0; rangeIndex < layouts[layout].Ranges.Size(); ++rangeIndex)
			{
				const BindingRangeDesc range = layouts[layout].Range(rangeIndex);
				fixed.Ranges[layout][rangeIndex] = range;
				// The sampler description is copied by the RHI layout owner; native fixed
				// realization receives the retained sampler as a descriptor resource.
				fixed.Ranges[layout][rangeIndex].ImmutableSamplerOrNull = nullptr;
				fixed.bHasStorage |= range.Type == EDescriptorType::StorageBuffer || range.Type == EDescriptorType::StorageTexture;
				fixed.bPreparedFixedCacheEligible &=
					range.Source == EBindingRangeSource::Fixed && range.Type != EDescriptorType::AccelerationStructure &&
					range.BufferMode != EBufferBindingMode::RootDescriptor;
				if (range.Source == EBindingRangeSource::Fixed)
				{
					fixed.FixedDescriptorCounts[layout] += range.DescriptorCount;
				}
				const D3D12_DESCRIPTOR_RANGE_TYPE type = range.Type == EDescriptorType::Sampler ? D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
					: range.Type == EDescriptorType::UniformBuffer ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV
					: range.Type == EDescriptorType::ReadOnlyBuffer ||
					range.Type == EDescriptorType::SampledTexture ||
					range.Type == EDescriptorType::AccelerationStructure
					? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
					: D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
				ranges[index] = { type, range.DescriptorCount, range.Binding, layouts[layout].Space, 0 };
				D3D12_ROOT_PARAMETER& parameter = parameters[rootOffset + index];
				if (range.BufferMode == EBufferBindingMode::RootDescriptor)
				{
					parameter.ParameterType = range.Type == EDescriptorType::UniformBuffer ? D3D12_ROOT_PARAMETER_TYPE_CBV :
						range.Type == EDescriptorType::ReadOnlyBuffer ? D3D12_ROOT_PARAMETER_TYPE_SRV :
						D3D12_ROOT_PARAMETER_TYPE_UAV;
					parameter.Descriptor = { range.Binding, layouts[layout].Space };
				}
				else
				{
					parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
					parameter.DescriptorTable = { 1, &ranges[index] };
				}
				const core::uint32 visibility = static_cast<core::uint32>(range.Visibility) & 14366u;
				parameter.ShaderVisibility = bGraphics && visibility == 2 ? D3D12_SHADER_VISIBILITY_VERTEX
					: bGraphics && visibility == 4 ? D3D12_SHADER_VISIBILITY_PIXEL
					: bGraphics && visibility == 8 ? D3D12_SHADER_VISIBILITY_MESH
					: bGraphics && visibility == 16 ? D3D12_SHADER_VISIBILITY_AMPLIFICATION
					: bGraphics && visibility == 2048 ? D3D12_SHADER_VISIBILITY_GEOMETRY
					: bGraphics && visibility == 4096 ? D3D12_SHADER_VISIBILITY_HULL
					: bGraphics && visibility == 8192 ? D3D12_SHADER_VISIBILITY_DOMAIN
					: D3D12_SHADER_VISIBILITY_ALL;
				++index;
			}
		}
	}
}
#endif
