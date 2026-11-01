#include "DeviceState.h"

#include <ScopeExit.hpp>

namespace rox::rhi
{
	using namespace core;

	namespace
	{
		struct NormalizedBindingSet final
		{
			BindingLayoutHandle Layout;

			Span<const TextureHandle> SampledTextures;
			Span<const BufferHandle> Buffers;
			Span<const TextureHandle> StorageTextures;
			Span<const SamplerHandle> Samplers;

			Span<const BufferRange> BufferRanges;

			Span<const TextureViewHandle> SampledViews;
			Span<const TextureViewHandle> StorageViews;
			Span<const BufferViewHandle> BufferViews;

			Span<const AccelStructHandle> AccelStructs;

			Span<const BindingTable> Tables;
		};
	}

	[[nodiscard]] Result<SamplerHandle> Device::CreateSampler(const SamplerDesc& desc)
	{
		CallScope call(*this, false);

		ASSERT(IsValidSampler(desc), "Sampler creation requires a valid descriptor.");

		if (!m_State->m_Backend.Capabilities.Features.bGraphics && !m_State->m_Backend.Capabilities.Features.bCompute)
		{
			return Result<SamplerHandle>::Failure(EErrorCode::Unsupported, "Rhi.Sampler", "The backend has no shader sampling capability.");
		}

		Result<void*> created = backend::CreateSampler(m_State->m_Backend.State, desc);

		if (!created)
		{
			return Result<SamplerHandle>::Failure(created.GetError());
		}

		const bool bNewSlot = m_State->m_FreeSampler == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Samplers.Size()) : m_State->m_FreeSampler;

		if (bNewSlot && (m_State->m_Samplers.Size() >= NumericLimits<uint32>::max() || !m_State->m_Samplers.Add(SamplerSlot{})))
		{
			backend::DestroySampler(m_State->m_Backend.State, created.Value());

			return Result<SamplerHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a sampler slot.");
		}
		SamplerSlot& slot = m_State->m_Samplers[index];

		if (!bNewSlot)
		{
			m_State->m_FreeSampler = slot.NextFree;
		}
		slot.Native = created.Value();
		slot.Descriptor = desc;
		slot.LastUse = 0;
		slot.RecordedUses = 0;
		slot.BindingSetRefs = 0;
		slot.NextFree = NumericLimits<uint32>::max();
		slot.bLive = true;

		return Result<SamplerHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroySampler(SamplerHandle handle)
	{
		CallScope call(*this, false);
		SamplerSlot* const slot = findSampler(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->BindingSetRefs != 0 || slot->RecordedUses != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.Sampler", "A binding set or recorded command still uses the sampler.");
		}

		if (slot->LastUse != 0)
		{
			Result<bool> complete = backend::IsComplete(m_State->m_Backend.State, slot->LastUse);

			if (!complete)
			{
				return Result<void>::Failure(complete.GetError());
			}

			if (!complete.Value())
			{
				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.Sampler", "The GPU has not completed use of the sampler.");
			}
		}

		backend::DestroySampler(m_State->m_Backend.State, slot->Native);
		slot->Native = nullptr;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeSampler;
			m_State->m_FreeSampler = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] SamplerSlot* Device::findSampler(SamplerHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Samplers.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		SamplerSlot& slot = m_State->m_Samplers[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] Result<BindingLayoutHandle> Device::createRangeBindingLayout(const BindingLayoutDesc& desc)
	{
		using R = Result<BindingLayoutHandle>;
		const auto invalidRange = []
			{
				return R::Failure(EErrorCode::Unsupported, "Rhi.BindingRange", "The binding range exceeds an implementation or device limit.");
			};
		ASSERT(desc.Kind <= EBindingLayoutKind::RayTracing, "A binding layout requires a valid pipeline kind.");
		if (desc.Ranges.Size() > MAXIMUM_BINDING_RANGES_PER_LAYOUT)
		{
			return invalidRange();
		}
		if ((desc.Kind == EBindingLayoutKind::Compute && !m_State->m_Backend.Capabilities.Features.bCompute) ||
			(desc.Kind == EBindingLayoutKind::Graphics && !m_State->m_Backend.Capabilities.Features.bGraphics) ||
			(desc.Kind == EBindingLayoutKind::RayTracing && !m_State->m_Backend.Capabilities.Features.bRayTracingPipeline))
		{
			return R::Failure(EErrorCode::Unsupported, "Rhi.BindingRange", "The requested pipeline stage family is unavailable.");
		}
		if (m_State->m_Kind == EBackend::Vulkan && (desc.Kind == EBindingLayoutKind::RayTracing ? desc.Space > 4 : desc.Space >= m_State->m_Backend.Capabilities.Limits.MaximumBindingLayouts))
		{
			return invalidRange();
		}
		BindingLayoutSlot prepared;
		prepared.RangeCount = static_cast<uint32>(desc.Ranges.Size());
		prepared.Space = desc.Space;
		prepared.Kind = desc.Kind;
		uint32 buffers = 0;
		uint32 sampled = 0;
		uint32 storage = 0;
		uint32 samplers = 0;
		uint32 accels = 0;
		const auto nativeNamespace = [](EDescriptorType type)
			{
				return type == EDescriptorType::UniformBuffer ? 0u
					: type == EDescriptorType::Sampler ? 1u
					: (type == EDescriptorType::SampledTexture || type == EDescriptorType::ReadOnlyBuffer ||
						type == EDescriptorType::AccelerationStructure)
					? 2u
					: 3u;
			};
		for (uint32 index = 0; index < prepared.RangeCount; ++index)
		{
			const rhi::BindingRangeDesc& range = desc.Ranges[index];
			const uint32 visibility = static_cast<uint32>(range.Visibility);
			const uint32 maximumDescriptors = range.Source == EBindingRangeSource::Fixed ? 8u : range.Type == EDescriptorType::SampledTexture ? m_State->m_Backend.MaximumBindlessSampledTextures : 4096u;
			ASSERT(range.Type <= EDescriptorType::AccelerationStructure &&
				range.DescriptorCount != 0 &&
				range.Source <= EBindingRangeSource::DescriptorTable &&
				visibility != 0 &&
				(visibility & ~16383u) == 0 &&
				(visibility & (desc.Kind == EBindingLayoutKind::Compute ? 1u : desc.Kind == EBindingLayoutKind::Graphics ? 14366u : 2016u)) != 0 &&
				(desc.Kind != EBindingLayoutKind::Compute || (visibility & ~7u) == 0) &&
				(desc.Kind != EBindingLayoutKind::Graphics || (visibility & ~14367u) == 0) &&
				(desc.Kind != EBindingLayoutKind::RayTracing || (visibility & ~2016u) == 0) &&
				(m_State->m_Kind != EBackend::D3D12 || uint64(range.Binding) + range.DescriptorCount <= 0x100000000ull),
				"A binding range must have valid type, count, source, visibility and register extent.");
			if (range.DescriptorCount > maximumDescriptors)
			{
				return invalidRange();
			}
			if (((visibility & 8u) != 0 && !m_State->m_Backend.Capabilities.Features.bMeshShader) ||
				((visibility & 16u) != 0 && !m_State->m_Backend.Capabilities.Features.bTaskShader) ||
				((visibility & 2048u) != 0 && !m_State->m_Backend.Capabilities.Features.bGeometryShader) ||
				((visibility & (4096u | 8192u)) != 0 && !m_State->m_Backend.Capabilities.Features.bTessellationShader))
			{
				return R::Failure(EErrorCode::Unsupported, "Rhi.BindingRange", "A requested graphics shader stage is unavailable.");
			}
			ASSERT(range.StructureStrideBytes == 0 || ((range.Type == EDescriptorType::ReadOnlyBuffer || range.Type == EDescriptorType::StorageBuffer) && range.BufferFormat == EFormat::UNKNOWN), "A structure stride requires an untyped buffer range.");
			if (m_State->m_Kind == EBackend::D3D12 && range.StructureStrideBytes > 2048)
			{
				return invalidRange();
			}
			const bool bBuffer = range.Type == EDescriptorType::UniformBuffer || range.Type == EDescriptorType::StorageBuffer || range.Type == EDescriptorType::ReadOnlyBuffer;
			ASSERT(range.BufferMode <= EBufferBindingMode::RootDescriptor &&
				(range.BufferMode != EBufferBindingMode::RootDescriptor ||
				 (bBuffer && range.Source == EBindingRangeSource::Fixed && range.DescriptorCount == 1 && range.BufferFormat == EFormat::UNKNOWN)) &&
				(range.ImmutableSamplerOrNull == nullptr ||
				 (range.Type == EDescriptorType::Sampler && range.Source == EBindingRangeSource::Fixed && range.DescriptorCount == 1 && IsValidSampler(*range.ImmutableSamplerOrNull))),
				"A binding range requires a valid buffer mode and immutable sampler.");
			if (bBuffer)
			{
				Result<void> format = validateBufferBindingFormat(range.BufferFormat, range.Type == EDescriptorType::UniformBuffer, range.Type == EDescriptorType::ReadOnlyBuffer);
				if (!format)
				{
					return R::Failure(format.GetError());
				}
			}
			else
			{
				ASSERT(range.BufferFormat == EFormat::UNKNOWN, "A buffer format requires a buffer range.");
			}
			for (uint32 previous = 0; previous < index; ++previous)
			{
				const rhi::BindingRangeDesc& prior = desc.Ranges[previous];
				const bool bOverlap = m_State->m_Kind == EBackend::Vulkan && !(desc.Kind == EBindingLayoutKind::RayTracing && desc.Space == 4)
					? prior.Binding == range.Binding
					: nativeNamespace(prior.Type) == nativeNamespace(range.Type) &&
					uint64(prior.Binding) < uint64(range.Binding) + range.DescriptorCount &&
					uint64(range.Binding) < uint64(prior.Binding) + prior.DescriptorCount;
				const uint32 previousVisibility = static_cast<uint32>(prior.Visibility) & 14366u;
				const uint32 currentVisibility = visibility & 14366u;
				// D3D12 can represent a single stage exactly; wider masks lower to ALL.
				const bool bDisjointNativeStages =
					m_State->m_Kind == EBackend::D3D12 &&
					desc.Kind == EBindingLayoutKind::Graphics &&
					previousVisibility != 0 &&
					currentVisibility != 0 &&
					(previousVisibility & (previousVisibility - 1)) == 0 &&
					(currentVisibility & (currentVisibility - 1)) == 0 &&
					previousVisibility != currentVisibility;
				ASSERT(!bOverlap || bDisjointNativeStages, "Binding ranges must not overlap in the same native namespace and stage.");
			}
			prepared.Entries[index].Type = range.Type;
			prepared.Entries[index].DescriptorCount = range.DescriptorCount;
			prepared.Entries[index].BufferFormat = range.BufferFormat;
			prepared.Entries[index].StructureStrideBytes = range.StructureStrideBytes;
			prepared.Entries[index].Source = range.Source;
			prepared.Entries[index].BufferMode = range.BufferMode;
			prepared.Entries[index].bImmutableSampler = range.ImmutableSamplerOrNull != nullptr;
			if (range.ImmutableSamplerOrNull != nullptr)
			{
				prepared.Entries[index].Sampler = *range.ImmutableSamplerOrNull;
			}
			prepared.Entries[index].Slot = range.Binding;
			prepared.Entries[index].RegisterSlot = range.Binding;
			prepared.Entries[index].StageMask = visibility;
			prepared.Entries[index].ResourceOrdinal = bBuffer ? buffers
				: range.Type == EDescriptorType::SampledTexture ? sampled
				: range.Type == EDescriptorType::StorageTexture ? storage
				: range.Type == EDescriptorType::Sampler ? samplers
				: accels;
			if (range.Source == EBindingRangeSource::DescriptorTable)
			{
				continue;
			}
			for (uint32 element = 0; element < range.DescriptorCount; ++element)
			{
				if (bBuffer)
				{
					if (buffers == 8)
					{
						return invalidRange();
					}

					prepared.BufferEntries[buffers] = static_cast<uint8>(index);
					++buffers;
				}
				else if (range.Type == EDescriptorType::Sampler)
				{
					if (++samplers > 8)
					{
						return invalidRange();
					}
				}
				else if (range.Type == EDescriptorType::AccelerationStructure)
				{
					if (!m_State->m_Backend.Capabilities.Features.bAccelerationStructure || accels == 8)
					{
						return invalidRange();
					}
					++accels;
				}
				else
				{
					if (sampled + storage == 8)
					{
						return invalidRange();
					}
					if (desc.Kind == EBindingLayoutKind::Compute)
					{
						prepared.TextureEntries[sampled + storage] = static_cast<uint8>(index);
					}
					else if (range.Type == EDescriptorType::SampledTexture)
					{
						prepared.TextureEntries[sampled] = static_cast<uint8>(index);
					}
					else
					{
						prepared.StorageTextureEntries[storage] = static_cast<uint8>(index);
					}
					if (range.Type == EDescriptorType::SampledTexture)
					{
						++sampled;
					}
					else
					{
						++storage;
					}
				}
			}
		}
		prepared.RangeSamplerCount = samplers;
		prepared.RangeAccelStructCount = accels;
		if (desc.Kind == EBindingLayoutKind::Compute)
		{
			prepared.ComputeCount = buffers;
			prepared.ComputeTextureCount = sampled + storage;
			prepared.ComputeSampledCount = sampled;
			prepared.ComputeStorageCount = storage;
		}
		else
		{
			prepared.GraphicsBufferCount = buffers;
			prepared.GraphicsCount = sampled;
			prepared.GraphicsStorageCount = storage;
			if (desc.Kind == EBindingLayoutKind::RayTracing)
			{
				prepared.ComputeCount = buffers;
				prepared.ComputeTextureCount = sampled + storage;
				prepared.ComputeSampledCount = sampled;
				prepared.ComputeStorageCount = storage;
			}
		}
		SamplerHandle immutableSamplers[8]{};
		uint32 immutableSamplerCount = 0;
		ScopeExit rollbackSamplers([&]() noexcept
			{
				for (uint32 index = 0; index < immutableSamplerCount; ++index)
				{
					const Result<void> destroyed = DestroySampler(immutableSamplers[index]);
					ASSERT(destroyed, "Unpublished immutable samplers can be released.");
				}
			});
		for (uint32 rangeIndex = 0; rangeIndex < prepared.RangeCount; ++rangeIndex)
		{
			BindingLayoutEntry& range = prepared.Entries[rangeIndex];
			if (!range.bImmutableSampler)
			{
				continue;
			}

			Result<SamplerHandle> sampler = CreateSampler(range.Sampler);
			if (!sampler)
			{
				return R::Failure(Move(sampler.GetError()));
			}
			range.ImmutableSampler = sampler.Value();
			immutableSamplers[immutableSamplerCount++] = sampler.Value();
		}

		const bool bNewSlot = m_State->m_FreeBindingLayout == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_BindingLayouts.Size()) : m_State->m_FreeBindingLayout;
		if (bNewSlot && (m_State->m_BindingLayouts.Size() >= NumericLimits<uint32>::max() || !m_State->m_BindingLayouts.Add(BindingLayoutSlot{})))
		{
			return R::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a binding layout.");
		}
		rhi::BindingLayoutSlot& destination = m_State->m_BindingLayouts[index];
		if (!bNewSlot)
		{
			m_State->m_FreeBindingLayout = destination.NextFree;
		}
		prepared.Generation = destination.Generation;
		prepared.bLive = true;
		destination = prepared;
		rollbackSamplers.Release();

		return R::Success({ m_State->m_Owner, index, destination.Generation });
	}

	[[nodiscard]] Result<void> Device::recordRangeBindingSets(CommandSlot& command, void* pipeline, bool bGraphics, Span<const BindingSetHandle> handles)
	{
		if (handles.IsEmpty())
		{
			return Result<void>::Success();
		}
		ASSERT(backend::HAS_RECORD_FIXED_BINDING_SETS, "A range pipeline has a native descriptor writer.");
		NativeFixedDescriptor fixedDescriptors[MAXIMUM_FIXED_BINDING_LAYOUTS][32]{};
		NativeFixedBindingSet sets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		PreparedBinding binding;
		binding.SetCount = static_cast<uint32>(handles.Size());
		for (uint32 index = 0; index < handles.Size(); ++index)
		{
			binding.Sets[index] = handles[index];
		}
		fillPreparedNativeSets(binding, fixedDescriptors, sets);
		Result<void> tableUses = prepareOrdinaryBoundTableUses(command, handles);
		if (!tableUses)
		{
			return tableUses;
		}
		if (!command.DescriptorTableUses.Reserve(command.DescriptorTableUses.Size() + handles.Size() * 8))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain native descriptor table versions.");
		}
		bool bStorageTable = false;
		for (const rhi::BindingSetHandle handle : handles)
		{
			rhi::BindingSetSlot const& set = m_State->m_BindingSets[handle.Slot];
			if (set.Tables)
			{
				for (rhi::BoundDescriptorTableRange const& row : set.Tables->Ranges)
				{
					bStorageTable |= m_State->m_DescriptorTables[row.Table.Slot].Descriptor.Type == EDescriptorType::StorageBuffer;
				}
			}
		}
		if (bStorageTable && (!bGraphics || m_State->m_Kind != EBackend::Vulkan))
		{
			Result<void> barrier = backend::RecordShaderMemoryBarrier(m_State->m_Backend.State, command.Native);
			if (!barrier)
			{
				return barrier;
			}
		}
		Result<void> recorded =
			backend::RecordFixedBindingSets(m_State->m_Backend.State, command.Native, pipeline, bGraphics, { sets, handles.Size() });
		if (recorded)
		{
			for (const rhi::BindingSetHandle handle : handles)
			{
				rhi::BindingSetSlot const& set = m_State->m_BindingSets[handle.Slot];
				if (set.Tables)
				{
					for (rhi::BoundDescriptorTableRange const& row : set.Tables->Ranges)
					{
						trackBoundTableUses(command, row, *set.Tables);
					}
				}
			}
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::validateRangePipelineLayouts(Span<const BindingLayoutHandle> handles, EBindingLayoutKind kind, bool bAdditionalBindings)
	{
		if (handles.Size() > MAXIMUM_FIXED_BINDING_LAYOUTS || handles.Size() > m_State->m_Backend.Capabilities.Limits.MaximumBindingLayouts)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BindingRange", "The fixed layout count exceeds device limits.");
		}
		for (uint32 index = 0; index < handles.Size(); ++index)
		{
			const BindingLayoutSlot* layout = findBindingLayout(handles[index]);
			ASSERT(layout != nullptr, "RHI operations require valid handles for this device.");
			ASSERT(layout->Kind == kind, "A layout must match its pipeline family.");
			for (uint32 previous = 0; previous < index; ++previous)
			{
				const BindingLayoutSlot* prior = findBindingLayout(handles[previous]);
				ASSERT(prior->Space != layout->Space, "Layout spaces must be distinct.");
			}
		}
		if (!handles.IsEmpty() && (bAdditionalBindings || backend::HAS_RECORD_FIXED_BINDING_SETS == false))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BindingRange", "The backend cannot combine these pipeline bindings.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] uint32 Device::rangeBufferStructureStride(const BindingLayoutSlot& layout, usize binding) noexcept
	{
		for (uint32 index = 0; index < layout.RangeCount; ++index)
		{
			const BindingRangeDesc& range = layout.Range(index);
			if (range.Source == EBindingRangeSource::DescriptorTable ||
				(range.Type != EDescriptorType::UniformBuffer && range.Type != EDescriptorType::ReadOnlyBuffer && range.Type != EDescriptorType::StorageBuffer))
			{
				continue;
			}
			if (binding < range.DescriptorCount)
			{
				return range.StructureStrideBytes;
			}
			binding -= range.DescriptorCount;
		}

		return 0;
	}

	[[nodiscard]] Result<BindingLayoutHandle> Device::CreateBindingLayout(const BindingLayoutDesc& desc)
	{
		CallScope call(*this, false);

		return createRangeBindingLayout(desc);
	}

	[[nodiscard]] Result<void> Device::DestroyBindingLayout(BindingLayoutHandle handle)
	{
		CallScope call(*this, false);
		BindingLayoutSlot* const slot = findBindingLayout(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->References != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.BindingLayout", "A pipeline or binding set still references the layout.");
		}
		for (uint32 index = 0; index < slot->RangeCount; ++index)
		{
			const SamplerHandle samplerHandle = slot->Entries[index].ImmutableSampler;
			if (!samplerHandle.IsValid())
			{
				continue;
			}
			SamplerSlot* const sampler = findSampler(samplerHandle);
			ASSERT(sampler != nullptr, "A binding layout owns its immutable samplers.");
			if (sampler->BindingSetRefs != 0 || sampler->RecordedUses != 0)
			{
				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.BindingLayout", "An immutable sampler is still recorded or retained.");
			}
			if (sampler->LastUse != 0)
			{
				Result<bool> complete = backend::IsComplete(m_State->m_Backend.State, sampler->LastUse);
				if (!complete)
				{
					return Result<void>::Failure(Move(complete.GetError()));
				}
				if (!complete.Value())
				{
					return Result<void>::Failure(EErrorCode::Conflict, "Rhi.BindingLayout", "An immutable sampler is still in GPU use.");
				}
			}
		}
		for (uint32 index = 0; index < slot->RangeCount; ++index)
		{
			const SamplerHandle samplerHandle = slot->Entries[index].ImmutableSampler;
			if (!samplerHandle.IsValid())
			{
				continue;
			}
			SamplerSlot& sampler = m_State->m_Samplers[samplerHandle.Slot];
			backend::DestroySampler(m_State->m_Backend.State, sampler.Native);
			sampler.Native = nullptr;
			sampler.bLive = false;
			Advance(sampler);

			if (!sampler.bExhausted)
			{
				sampler.NextFree = m_State->m_FreeSampler;
				m_State->m_FreeSampler = samplerHandle.Slot;
			}
			slot->Entries[index].ImmutableSampler =
			{
			};
		}
		slot->ComputeCount = 0;
		slot->ComputeTextureCount = 0; slot->ComputeSampledCount = 0; slot->ComputeStorageCount = 0;
		slot->GraphicsCount = 0;
		slot->GraphicsStorageCount = 0;
		slot->GraphicsBufferCount = 0;
		slot->bLive = false;
		Advance(*
			slot);

		if (!
			slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeBindingLayout;
			m_State->m_FreeBindingLayout = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] BindingLayoutSlot* Device::findBindingLayout(BindingLayoutHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_BindingLayouts.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		BindingLayoutSlot&
			slot = m_State->m_BindingLayouts[handle.Slot];
		return
			slot.bLive && slot.Generation == handle.Generation ? &
			slot : nullptr;
	}

	[[nodiscard]] Result<BindingSetHandle> Device::CreateBindingSet(const BindingSetDesc& input)
	{
		ROX_PROFILE_DEEP_SCOPE("RHI", "CreateBindingSet");
		CallScope call(*this, false);
		BindingLayoutSlot* const layout = findBindingLayout(input.Layout);

		ASSERT(layout != nullptr, "RHI operations require valid handles for this device.");
		NormalizedBindingSet desc;
		desc.Layout = input.Layout;
		desc.Tables = input.Tables;
		BufferHandle buffers[8]{};
		BufferRange bufferRanges[8]{};
		BufferViewHandle bufferViews[8]{};
		TextureHandle sampledTextures[8]{};
		TextureViewHandle sampledTextureViews[8]{};
		TextureHandle storageTextures[8]{};
		TextureViewHandle storageTextureViews[8]{};
		SamplerHandle samplers[8]{};
		AccelStructHandle accelerationStructures[8]{};
		bool seen[MAXIMUM_BINDING_RANGES_PER_LAYOUT][8]{
		};

		for (uint32 index = 0; index < layout->RangeCount; ++index)
		{
			const
				BindingLayoutEntry& range = layout->Entries[index];
			if (range.bImmutableSampler)
			{
				samplers[range.ResourceOrdinal] = range.ImmutableSampler;
			}
		}
		ASSERT(layout->RangeCount != 0 || input.Entries.IsEmpty(), "Binding entries require a range layout.");
		for (const BindingEntry& binding : input.Entries)
		{
			ASSERT(binding.RangeIndex < layout->RangeCount && binding.ArrayElement < 8, "A binding coordinate is out of range.");
			const BindingLayoutEntry& range = layout->Entries[binding.RangeIndex];
			ASSERT(range.Source == EBindingRangeSource::Fixed && !range.bImmutableSampler && binding.ArrayElement < range.DescriptorCount && !seen[binding.RangeIndex][binding.ArrayElement],
				"A binding coordinate is duplicate or not a mutable fixed element.");
			seen[binding.RangeIndex][binding.ArrayElement] = true;
			const uint32 ordinal = range.ResourceOrdinal + binding.ArrayElement;
			const DescriptorValue& value = binding.Value;
			const EDescriptorValueKind kind = value.GetKind();
			if ((range.Type == EDescriptorType::UniformBuffer || range.Type == EDescriptorType::StorageBuffer || range.Type == EDescriptorType::ReadOnlyBuffer) && kind == EDescriptorValueKind::Buffer)
			{
				buffers[ordinal] = value.GetBuffer();
				bufferRanges[ordinal] = value.GetBufferRange();
			}
			else if ((range.Type == EDescriptorType::StorageBuffer || range.Type == EDescriptorType::ReadOnlyBuffer) && kind == EDescriptorValueKind::BufferView)
			{
				bufferViews[ordinal] = value.GetBufferView();
				BufferViewSlot* const view = findBufferView(bufferViews[ordinal]);
				ASSERT(view != nullptr, "RHI operations require valid handles for this device.");
				buffers[ordinal] = view->Descriptor.Buffer;
			}
			else if (range.Type == EDescriptorType::SampledTexture && kind == EDescriptorValueKind::SampledTexture)
			{
				sampledTextures[ordinal] = value.GetTexture();
				ASSERT(sampledTextures[ordinal].IsValid(), "RHI operations require valid handles for this device.");
			}
			else if (range.Type == EDescriptorType::SampledTexture && kind == EDescriptorValueKind::SampledView)
			{
				sampledTextureViews[ordinal] = value.GetTextureView();
				ASSERT(sampledTextureViews[ordinal].IsValid(), "RHI operations require valid handles for this device.");
			}
			else if (range.Type == EDescriptorType::StorageTexture && kind == EDescriptorValueKind::StorageTexture)
			{
				storageTextures[ordinal] = value.GetTexture();
				ASSERT(storageTextures[ordinal].IsValid(), "RHI operations require valid handles for this device.");
			}
			else if (range.Type == EDescriptorType::StorageTexture && kind == EDescriptorValueKind::StorageView)
			{
				storageTextureViews[ordinal] = value.GetTextureView();

				ASSERT(storageTextureViews[ordinal].IsValid(), "RHI operations require valid handles for this device.");
			}
			else

				if (range.Type == EDescriptorType::Sampler && kind == EDescriptorValueKind::Sampler)
				{
					samplers[ordinal] = value.GetSampler();
				}
				else if (range.Type == EDescriptorType::AccelerationStructure && kind == EDescriptorValueKind::AccelerationStructure)
				{
					accelerationStructures[ordinal] = value.GetAccelerationStructure();
				}
				else
				{
					ASSERT(false, "A descriptor value does not match its range.");
				}
		}
		for (uint32 rangeIndex = 0; rangeIndex < layout->RangeCount; ++rangeIndex)
		{
			const BindingLayoutEntry& range = layout->Entries[rangeIndex];

			if (range.Source != EBindingRangeSource::Fixed || range.bImmutableSampler)
			{
				continue;
			}
			for (uint32 element = 0; element < range.DescriptorCount; ++element)
			{
				ASSERT(seen[rangeIndex][element], "A fixed descriptor element is missing.");
			}
		}
		desc.Buffers = { buffers,(layout->Kind == EBindingLayoutKind::Compute ? layout->ComputeCount : layout->GraphicsBufferCount) };
		desc.BufferRanges = { bufferRanges, desc.Buffers.Size()
		};
		desc.BufferViews = { bufferViews, desc.Buffers.Size() };
		desc.SampledTextures = { sampledTextures, (layout->Kind == EBindingLayoutKind::Compute ? layout->ComputeSampledCount : layout->GraphicsCount)
		}; desc.SampledViews =
		{ sampledTextureViews, desc.SampledTextures.Size() };
		desc.StorageTextures = { storageTextures, (layout->Kind == EBindingLayoutKind::Compute ? layout->ComputeStorageCount : layout->GraphicsStorageCount) };
		desc.StorageViews = { storageTextureViews,desc.StorageTextures.Size() };
		desc.Samplers = { samplers,layout->RangeSamplerCount };
		desc.AccelStructs = { accelerationStructures, layout->RangeAccelStructCount };

		Result<SharedPtr<BoundDescriptorTables>> tables = prepareBoundDescriptorTables(*layout, desc.Tables);
		if (!tables)
		{
			return Result<BindingSetHandle>::Failure(Move(tables.GetError()));
		}

		const bool bSampledViews = !desc.SampledViews.IsEmpty();
		const bool bStorageViews = !desc.StorageViews.IsEmpty();
		const usize sampledCount = bSampledViews ? desc.SampledViews.Size() : desc.SampledTextures.Size();
		const usize storageCount = bStorageViews ? desc.StorageViews.Size() : desc.StorageTextures.Size();

		ASSERT(sampledCount == (layout->Kind == EBindingLayoutKind::Compute ? layout->ComputeSampledCount : layout->GraphicsCount) &&
			(layout->RangeCount != 0 ? desc.Samplers.Size() == layout->RangeSamplerCount : (desc.Samplers.IsEmpty() || desc.Samplers.Size() == sampledCount)) &&
			storageCount == (layout->Kind == EBindingLayoutKind::Compute ? layout->ComputeStorageCount : layout->GraphicsStorageCount) &&
			desc.Buffers.Size() == (layout->Kind == EBindingLayoutKind::Compute ? layout->ComputeCount : layout->GraphicsBufferCount) &&
			desc.AccelStructs.Size() == layout->RangeAccelStructCount &&
			(desc.BufferRanges.IsEmpty() || desc.BufferRanges.Size() == desc.Buffers.Size()) &&
			(desc.BufferViews.IsEmpty() || desc.BufferViews.Size() == desc.Buffers.Size()),
			"The resources must match the layout.");
		if (layout->References == NumericLimits<uint32>::max())
		{
			return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSet", "Too many layout references.");
		}

		for (const rhi::AccelStructHandle handle : desc.AccelStructs)
		{
			const AccelStructSlot* accel = findAccelStruct(handle);
			ASSERT(accel != nullptr && accel->Type == EAccelStructType::TopLevel, "A fixed acceleration structure must be a valid top level handle.");
			if (accel->bUnavailable)
			{
				return Result<BindingSetHandle>::Failure(EErrorCode::Conflict, "Rhi.BindingSet", "A fixed AS element must retain an available TLAS.");
			}
			if (
				accel->BindingSetRefs > UINT32_MAX - 8)
			{
				return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSet", "Too many acceleration structure references.");
			}
		}

		const bool bCompute = layout->Kind == EBindingLayoutKind::Compute;

		if ((bCompute && !m_State->m_Backend.Capabilities.Features.bCompute) ||
			(layout->Kind == EBindingLayoutKind::RayTracing ? !m_State->m_Backend.Capabilities.Features.bRayTracingPipeline : !bCompute && !m_State->m_Backend.Capabilities.Features.bGraphics))
		{
			return Result<BindingSetHandle>::Failure(EErrorCode::Unsupported, "Rhi.BindingSet", "The binding set kind is unavailable.");
		}

		ASSERT(sampledCount + storageCount <= 8 && desc.Buffers.Size() <= 8 && ! (layout->ComputeTextureCount == 0 && bCompute && sampledCount != 0),
			"The binding set resource counts must match the layout limits.");

		for (uint32 binding = 0; binding < desc.Buffers.Size(); ++binding)
		{
			const BufferHandle handle = desc.Buffers[binding];
			BufferSlot* const buffer = findBuffer(handle);

			ASSERT(buffer != nullptr, "RHI operations require valid handles for this device.");

			const bool bUniform = bCompute ? layout->ComputeBuffer(binding).Type == EComputeBindingType::UniformBuffer
				: layout->GraphicsBuffer(binding).Type == EGraphicsBufferBindingType::UniformBuffer;
			const EBufferUsage required = bUniform ? EBufferUsage::Uniform : EBufferUsage::Storage;

			ASSERT(HasUsage(buffer->Usage, required), "A buffer must have the usage required by its binding range.");
			if (buffer->BindingSetRefs > NumericLimits<uint32>::max() - 8)
			{
				return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSet", "Too many buffer references.");
			}

			const EFormat format = bCompute ? layout->ComputeBuffer(binding).Format : layout->GraphicsBuffer(binding).Format;
			const bool bReadOnly = bCompute ? layout->ComputeBuffer(binding).Type == EComputeBindingType::ReadOnlyBuffer
				: layout->GraphicsBuffer(binding).Type == EGraphicsBufferBindingType::ReadOnlyBuffer;
			BufferViewSlot* const view = !desc.BufferViews.IsEmpty() && desc.BufferViews[binding].IsValid() ? findBufferView(desc.BufferViews[binding]) : nullptr;

			ASSERT((format != EFormat::UNKNOWN &&
					view != nullptr &&
					view->Descriptor.Buffer == handle &&
					view->Descriptor.Format == format &&
					(view->Descriptor.Type == EBufferViewType::ReadOnly) == bReadOnly) ||
				(format == EFormat::UNKNOWN && (desc.BufferViews.IsEmpty() || !desc.BufferViews[binding].IsValid())),
				"A buffer view must match the range format, type, and buffer.");
			if
				(view != nullptr && view->BindingSetRefs > NumericLimits<uint32>::max() - 8)
			{
				return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSet", "Too many buffer view references.");
			}

			const BufferRange explicitRange = desc.BufferRanges.IsEmpty() ? BufferRange{} : desc.BufferRanges[binding];
			ASSERT(view == nullptr ||
				(explicitRange.ByteOffset == 0 && explicitRange.ByteCount == 0) ||
				(explicitRange.ByteOffset == view->Descriptor.Range.ByteOffset && explicitRange.ByteCount == view->Descriptor.Range.ByteCount),
				"An explicit buffer range must agree with its typed view.");

			if (view != nullptr)
			{
				continue;
			}

			const uint32 structureStride = rangeBufferStructureStride(*layout, binding);
			if (!bUniform && layout->RangeCount != 0)
			{
				const uint32 elementBytes = structureStride != 0 ? structureStride : 4u;
				BufferRange fixedRange = explicitRange;
				if (fixedRange.ByteCount == 0 && fixedRange.ByteOffset == 0)
				{
					fixedRange.ByteCount = buffer->ByteCount;
				}
				const uint64 alignment = m_State->m_Backend.Capabilities.Limits.StorageBufferOffsetAlignmentBytes;
				ASSERT(alignment != 0 &&
					fixedRange.ByteCount != 0 &&
					fixedRange.ByteOffset % alignment == 0 &&
					fixedRange.ByteOffset % elementBytes == 0 &&
					fixedRange.ByteCount % elementBytes == 0 &&
					Fits(buffer->ByteCount, fixedRange.ByteOffset, fixedRange.ByteCount),
					"A fixed buffer range must fit native size, alignment and element limits.");
				if (fixedRange.ByteCount > m_State->m_Backend.Capabilities.Limits.MaximumStorageBufferRangeBytes ||
					(m_State->m_Kind == EBackend::D3D12 && fixedRange.ByteCount / elementBytes > NumericLimits<uint32>::max()))
				{
					return Result<BindingSetHandle>::Failure(EErrorCode::Unsupported, "Rhi.BindingRange", "The storage buffer range exceeds the device descriptor limit.");
				}
				continue;
			}

			const BufferRange range = explicitRange;

			if (range.ByteOffset != 0 || range.ByteCount != 0)
			{
				const uint64 alignment = bUniform ? 256 : m_State->m_Backend.Capabilities.Limits.StorageBufferOffsetAlignmentBytes;

				ASSERT(alignment != 0 &&
					range.ByteOffset % alignment == 0 &&
					range.ByteOffset % 4 == 0 &&
					range.ByteCount != 0 &&
					range.ByteCount % (bUniform ? 256 : 4) == 0 &&
					(!bUniform || range.ByteCount <= 65536) &&
					Fits(buffer->ByteCount, range.ByteOffset, range.ByteCount),
					"A shader buffer range must satisfy size and alignment requirements.");
			}
		}

		for (const TextureHandle handle : desc.SampledTextures)
		{
			if (!handle.IsValid())
			{
				continue;
			}
			TextureSlot* const texture = findTexture(handle);

			ASSERT(texture != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(HasUsage(texture->Descriptor.Usage, ETextureUsage::ShaderRead), "A sampled texture must have ShaderRead usage.");
			if (
				texture->BindingSetRefs > NumericLimits<uint32>::max() - 8)
			{
				return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSet", "Too many sampled texture references.");
			}
		}

		for (const TextureHandle handle : desc.StorageTextures)
		{
			if (!handle.IsValid())
			{
				continue;
			}
			TextureSlot* const texture = findTexture(handle);

			ASSERT(texture != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(HasUsage(texture->Descriptor.Usage, ETextureUsage::Storage) || texture->bSamplerFeedback, "A storage texture must have Storage usage.");
			if (
				texture->BindingSetRefs > NumericLimits<uint32>::max() - 8)
			{
				return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSet", "Too many storage texture references.");
			}
		}

		for (const TextureViewHandle handle : desc.SampledViews)
		{
			if (!handle.IsValid())
			{
				continue;
			}
			TextureViewSlot* const view = findTextureView(handle);

			ASSERT(view != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(view->Descriptor.Type == ETextureViewType::Sampled, "A texture view must match its binding range type.");
			if (view->BindingSetRefs > NumericLimits<uint32>::max() - 8 || m_State->m_Textures[view->Descriptor.Texture.Slot].BindingSetRefs > NumericLimits<uint32>::max() - 8)
			{
				return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSet", "Too many sampled view references.");
			}
		}

		for (const TextureViewHandle handle : desc.StorageViews)
		{
			if (!handle.IsValid())
			{
				continue;
			}
			TextureViewSlot* const view = findTextureView(handle);

			ASSERT(view != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(view->Descriptor.Type == ETextureViewType::Storage, "A texture view must match its binding range type.");
			if (view->BindingSetRefs > NumericLimits<uint32>::max() - 8 || m_State->m_Textures[view->Descriptor.Texture.Slot].BindingSetRefs > NumericLimits<uint32>::max() - 8)
			{
				return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSet", "Too many storage view references.");
			}
		}

		for (const SamplerHandle handle : desc.Samplers)
		{
			SamplerSlot* const sampler = findSampler(handle);

			ASSERT(sampler != nullptr, "RHI operations require valid handles for this device.");

			if (sampler->BindingSetRefs > NumericLimits<uint32>::max() - 8)
			{
				return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSet", "Too many sampler references.");
			}
		}

		SharedPtr<PreparedBindingCache> cache;
		if (!MakeSharedInto(m_State->m_Resource, &cache))
		{
			return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain descriptor realization cache.");
		}

		const bool bNewSlot = m_State->m_FreeBindingSet == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_BindingSets.Size()) : m_State->m_FreeBindingSet;

		if (bNewSlot && (m_State->m_BindingSets.Size() >= NumericLimits<uint32>::max() || !m_State->m_BindingSets.Add(BindingSetSlot{})))
		{
			return Result<BindingSetHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a binding set slot.");
		}
		BindingSetSlot& slot = m_State->m_BindingSets[index];

		if (!bNewSlot)
		{
			m_State->m_FreeBindingSet = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.Layout = desc.Layout;
		slot.Tables = Move(tables).TakeValue();
		slot.Cache = Move(cache);
		if (slot.Tables)
		{
			for (rhi::BoundDescriptorTableRange const& row : slot.Tables->Ranges)
			{
				++m_State->m_DescriptorTables[row.Table.Slot].BoundReferences;
			}
		}

		slot.TextureCount = static_cast<uint32>(sampledCount);
		slot.SamplerCount = static_cast<uint32>(desc.Samplers.Size());
		slot.StorageTextureCount = static_cast<uint32>(storageCount);
		slot.BufferCount = static_cast<uint32>(desc.Buffers.Size());
		slot.ShaderTableReferences = 0;
		slot.PreparedReferences = 0;

		for (uint32 binding = 0; binding < slot.TextureCount; ++binding)
		{
			const bool bHasView = bSampledViews && desc.SampledViews[binding].IsValid();
			slot.SampledViews[binding] = bHasView ? desc.SampledViews[binding] : TextureViewHandle{};
			slot.SampledTextures[binding] = bHasView ? m_State->m_TextureViews[slot.SampledViews[binding].Slot].Descriptor.Texture : desc.SampledTextures[binding];

			if (bHasView)
			{
				++m_State->m_TextureViews[slot.SampledViews[binding].Slot].BindingSetRefs;
			}
			++m_State->m_Textures[slot.SampledTextures[binding].Slot].BindingSetRefs;
		}

		for (uint32 binding = 0; binding < slot.SamplerCount; ++binding)
		{
			slot.Samplers[binding] = desc.Samplers[binding];
			++m_State->m_Samplers[slot.Samplers[binding].Slot].BindingSetRefs;
		}

		for (uint32 binding = 0; binding < slot.StorageTextureCount; ++binding)
		{
			const bool bHasView = bStorageViews && desc.StorageViews[binding].IsValid();
			slot.StorageViews[binding] = bHasView ? desc.StorageViews[binding] : TextureViewHandle{};
			slot.StorageTextures[binding] = bHasView ? m_State->m_TextureViews[slot.StorageViews[binding].Slot].Descriptor.Texture : desc.StorageTextures[binding];

			if (bHasView)
			{
				++m_State->m_TextureViews[slot.StorageViews[binding].Slot].BindingSetRefs;
			}
			++m_State->m_Textures[slot.StorageTextures[binding].Slot].BindingSetRefs;
		}

		for (uint32 binding = 0; binding < slot.BufferCount; ++binding)
		{
			slot.Buffers[binding] = desc.Buffers[binding];
			slot.BufferViews[binding] = desc.BufferViews.IsEmpty() ? BufferViewHandle{} : desc.BufferViews[binding];
			slot.BufferRanges[binding] = slot.BufferViews[binding].IsValid()
				? m_State->m_BufferViews[slot.BufferViews[binding].Slot].Descriptor.Range
				: desc.BufferRanges.IsEmpty() ? BufferRange{}
			: desc.BufferRanges[binding];

			if (slot.BufferViews[binding].IsValid())
			{
				++m_State->m_BufferViews[slot.BufferViews[binding].Slot].BindingSetRefs;
			}
			++m_State->m_Buffers[slot.Buffers[binding].Slot].BindingSetRefs;
		}

		slot.AccelStructCount = static_cast<uint32>(desc.AccelStructs.Size());
		for (uint32 binding = 0; binding < slot.AccelStructCount; ++binding)
		{
			slot.AccelStructs[binding] = desc.AccelStructs[binding];
			++m_State->m_AccelStructs[slot.AccelStructs[binding].Slot].BindingSetRefs;
		}
		slot.bLive = true;
		++layout->References;

		return Result<BindingSetHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyBindingSet(BindingSetHandle handle)
	{
		CallScope call(*this, false);
		BindingSetSlot* const slot = findBindingSet(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->ShaderTableReferences != 0 || slot->PreparedReferences != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.BindingSet", "A shader table still references the binding set.");
		}

		slot->Cache.Reset();

		for (uint32 binding = 0; binding < slot->TextureCount; ++binding)
		{
			--m_State->m_Textures[slot->SampledTextures[binding].Slot].BindingSetRefs;

			if (slot->SampledViews[binding].IsValid())
			{
				--m_State->m_TextureViews[slot->SampledViews[binding].Slot].BindingSetRefs;
			}
		}

		for (uint32 binding = 0; binding < slot->SamplerCount; ++binding)
		{
			--m_State->m_Samplers[slot->Samplers[binding].Slot].BindingSetRefs;
		}

		for (uint32 binding = 0; binding < slot->StorageTextureCount; ++binding)
		{
			--m_State->m_Textures[slot->StorageTextures[binding].Slot].BindingSetRefs;

			if (slot->StorageViews[binding].IsValid())
			{
				--m_State->m_TextureViews[slot->StorageViews[binding].Slot].BindingSetRefs;
			}
		}

		for (uint32 binding = 0; binding < slot->BufferCount; ++binding)
		{
			--m_State->m_Buffers[slot->Buffers[binding].Slot].BindingSetRefs;

			if (slot->BufferViews[binding].IsValid())
			{
				--m_State->m_BufferViews[slot->BufferViews[binding].Slot].BindingSetRefs;
			}
		}
		for (uint32 index = 0; index < slot->AccelStructCount; ++index)
		{
			--m_State->m_AccelStructs[slot->AccelStructs[index].Slot].BindingSetRefs;
		}
		slot->AccelStructCount = 0;
		if (slot->Tables)
		{
			for (rhi::BoundDescriptorTableRange const& row : slot->Tables->Ranges)
			{
				--m_State->m_DescriptorTables[row.Table.Slot].BoundReferences;
			}
			slot->Tables.Reset();
		}
		--m_State->m_BindingLayouts[slot->Layout.Slot].References;
		slot->Layout = {};
		slot->TextureCount = 0;
		slot->SamplerCount = 0;
		slot->StorageTextureCount = 0;
		slot->BufferCount = 0;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeBindingSet;
			m_State->m_FreeBindingSet = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] BindingSetSlot* Device::findBindingSet(BindingSetHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_BindingSets.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		BindingSetSlot& slot = m_State->m_BindingSets[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] BufferRange Device::resolveBufferRange(
		BindingSetHandle bindingSet,
		usize binding,
		uint64 fullByteCount,
		Span<const BufferViewHandle> inlineViews) noexcept
	{
		BindingSetSlot* const set = findBindingSet(bindingSet);

		if (set == nullptr && !inlineViews.IsEmpty())
		{
			if (BufferViewSlot* const view = findBufferView(inlineViews[binding]))
			{
				return view->Descriptor.Range;
			}
		}

		if (set == nullptr || set->BufferRanges[binding].ByteCount == 0)
		{
			return { 0, fullByteCount };
		}

		return set->BufferRanges[binding];
	}

	[[nodiscard]] bool Device::canShareBuffer(bool bCurrentReadOnly, bool bPreviousReadOnly, BufferRange current, BufferRange previous) noexcept
	{
		if (bCurrentReadOnly && bPreviousReadOnly)
		{
			return true;
		}

		if (bCurrentReadOnly || bPreviousReadOnly)
		{
			return false;
		}

		return current.ByteOffset + current.ByteCount <= previous.ByteOffset || previous.ByteOffset + previous.ByteCount <= current.ByteOffset;
	}

	[[nodiscard]] Span<const TextureHandle> Device::resolveGraphicsTextures(
		BindingLayoutHandle expectedLayout,
		BindingSetHandle bindingSet,
		Span<const TextureHandle> inlineTextures) noexcept
	{
		ASSERT(!expectedLayout.IsValid() || m_State->m_BindingLayouts[expectedLayout.Slot].RangeCount == 0 || bindingSet.IsValid(), "A range pipeline requires a complete binding snapshot.");
		if (bindingSet.Owner == 0 && bindingSet.Slot == 0 && bindingSet.Generation == 0)
		{
			return inlineTextures;
		}
		BindingSetSlot* const slot = findBindingSet(bindingSet);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->Layout == expectedLayout, "The set layout differs from the graphics pipeline.");

		ASSERT(inlineTextures.IsEmpty(), "A draw cannot use both inline textures and a binding set.");

		return { slot->SampledTextures, slot->TextureCount };
	}

	[[nodiscard]] Span<const TextureHandle> Device::resolveComputeTextures(
		BindingLayoutHandle expectedLayout,
		BindingSetHandle bindingSet,
		Span<const TextureHandle> inlineTextures) noexcept
	{
		if (bindingSet.Owner == 0 && bindingSet.Slot == 0 && bindingSet.Generation == 0)
		{
			return inlineTextures;
		}
		BindingSetSlot* const slot = findBindingSet(bindingSet);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->Layout == expectedLayout && inlineTextures.IsEmpty(), "The compute texture set or inline binding is incompatible.");

		return { slot->SampledTextures, slot->TextureCount };
	}

	[[nodiscard]] Span<const TextureHandle> Device::resolveComputeStorageTextures(
		BindingLayoutHandle expectedLayout,
		BindingSetHandle bindingSet,
		Span<const TextureHandle> inlineTextures) noexcept
	{
		if (bindingSet.Owner == 0 && bindingSet.Slot == 0 && bindingSet.Generation == 0)
		{
			return inlineTextures;
		}
		BindingSetSlot* const slot = findBindingSet(bindingSet);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->Layout == expectedLayout && inlineTextures.IsEmpty(), "The compute storage texture set or inline binding is incompatible.");

		return { slot->StorageTextures, slot->StorageTextureCount };
	}

	[[nodiscard]] Span<const TextureHandle> Device::resolveGraphicsStorageTextures(
		BindingLayoutHandle expectedLayout,
		BindingSetHandle bindingSet,
		Span<const TextureHandle> inlineTextures) noexcept
	{
		if (bindingSet.Owner == 0 && bindingSet.Slot == 0 && bindingSet.Generation == 0)
		{
			return inlineTextures;
		}
		BindingSetSlot* const slot = findBindingSet(bindingSet);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->Layout == expectedLayout && inlineTextures.IsEmpty(), "The graphics storage texture set or inline binding is incompatible.");

		return { slot->StorageTextures, slot->StorageTextureCount };
	}

	[[nodiscard]] Span<const BufferHandle> Device::resolveGraphicsBuffers(BindingLayoutHandle expectedLayout, BindingSetHandle bindingSet) noexcept
	{
		if (bindingSet.Owner == 0 && bindingSet.Slot == 0 && bindingSet.Generation == 0)
		{
			return {};
		}
		BindingSetSlot* const slot = findBindingSet(bindingSet);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->Layout == expectedLayout, "The set layout differs from the graphics pipeline.");

		return { slot->Buffers, slot->BufferCount };
	}

	[[nodiscard]] bool Device::textureViewsDisjoint(TextureViewHandle first, TextureViewHandle second) const noexcept
	{
		if (!first.IsValid() || !second.IsValid())
		{
			return false;
		}

		const TextureViewDesc& a = m_State->m_TextureViews[first.Slot].Descriptor;
		const TextureViewDesc& b = m_State->m_TextureViews[second.Slot].Descriptor;

		return a.BaseMipLevel + a.MipLevelCount <= b.BaseMipLevel ||
			b.BaseMipLevel + b.MipLevelCount <= a.BaseMipLevel ||
			a.BaseArrayLayer + a.ArrayLayerCount <= b.BaseArrayLayer ||
			b.BaseArrayLayer + b.ArrayLayerCount <= a.BaseArrayLayer;
	}

	[[nodiscard]] EFormat Device::sampledViewFormat(const BindingSetSlot* set, usize index, EFormat defaultFormat) const noexcept
	{
		if (set == nullptr || !set->SampledViews[index].IsValid())
		{
			return defaultFormat;
		}

		const EFormat format = m_State->m_TextureViews[set->SampledViews[index].Slot].Descriptor.Format;

		return format == EFormat::UNKNOWN ? defaultFormat : format;
	}

	[[nodiscard]] Result<void> Device::validateGraphicsBuffers(
		const GraphicsPipelineSlot& pipeline,
		Span<const BufferHandle> handles,
		BindingSetHandle bindingSet,
		BufferHandle vertex,
		BufferHandle index,
		BufferHandle arguments,
		NativeGraphicsBuffer* native) noexcept
	{
		ASSERT(handles.Size() == pipeline.BufferBindingCount, "The graphics buffer count does not match the pipeline.");

		for (usize binding = 0; binding < handles.Size(); ++binding)
		{
			BufferSlot* const buffer = findBuffer(handles[binding]);

			ASSERT(buffer != nullptr, "RHI operations require valid handles for this device.");

			const GraphicsBufferBindingDesc& declared = pipeline.BufferBindings[binding];
			const EBufferUsage required = declared.Type == EGraphicsBufferBindingType::UniformBuffer ? EBufferUsage::Uniform : EBufferUsage::Storage;
			const BufferRange range = resolveBufferRange(bindingSet, binding, buffer->ByteCount);

			ASSERT(HasUsage(buffer->Usage,
				required) &&
				(declared.Type != EGraphicsBufferBindingType::UniformBuffer || range.ByteCount <= 65536) &&
				handles[binding] != vertex &&
				handles[binding] != index &&
				handles[binding] != arguments,
				"A graphics buffer has incompatible usage or aliases another input.");

			for (usize previous = 0; previous < binding; ++previous)
			{
				ASSERT(handles[previous] != handles[binding] ||
					(declared.Type == pipeline.BufferBindings[previous].Type &&
							canShareBuffer(declared.Type != EGraphicsBufferBindingType::StorageBuffer, pipeline.BufferBindings[previous].Type != EGraphicsBufferBindingType::StorageBuffer, range, resolveBufferRange(bindingSet, previous, buffer->ByteCount))),
					"A graphics buffer is bound more than once.");
			}

			Result<void*> view = resolveBoundBufferView(bindingSet, binding, handles[binding], declared.Format, declared.Type == EGraphicsBufferBindingType::ReadOnlyBuffer);

			if (!view)
			{
				return Result<void>::Failure(view.GetError());
			}
			native[binding] = { buffer->Native,	declared.Type, declared.Slot,  range.ByteOffset,
							   range.ByteCount, view.Value(),  declared.Format };
		}

		return Result<void>::Success();
	}

	void Device::trackGraphicsBuffers(CommandSlot& command, const GraphicsPipelineSlot& pipeline, Span<const BufferHandle> handles) noexcept
	{
		for (usize index = 0; index < handles.Size(); ++index)
		{
			const BufferHandle handle = handles[index];
			(void)command.AddBufferUse({ handle, m_State->m_Buffers[handle.Slot].Native, BufferUseState(pipeline.BufferBindings[index].Type) });
			++m_State->m_Buffers[handle.Slot].RecordedUses;
		}
	}

	[[nodiscard]] Result<void> Device::resolveFixedGraphicsBindings(
		Span<const BindingSetHandle> sets,
		Span<const VertexBufferBinding> extraVertexBuffers,
		CommandSlot& command,
		const GraphicsPipelineSlot& pipeline,
		uint32 listSlot,
		BufferHandle vertex,
		BufferHandle index,
		BufferHandle arguments,
		BufferHandle count,
		FixedGraphicsBindings& output)
	{
		for (const BindingSetHandle handle : sets)
		{
			BindingSetSlot* const set = findBindingSet(handle);
			ASSERT(set != nullptr, "RHI operations require valid handles for this device.");
			if (set->Tables)
			{
				for (const BoundDescriptorTableRange& row : set->Tables->Ranges)
				{
					const DescriptorTableSlot& table = m_State->m_DescriptorTables[row.Table.Slot];
					for (uint32 used = 0; used < row.UsedCount; ++used)
					{
						const BufferHandle buffer = table.Entries[set->Tables->UsedEntries[row.UsedOffset + used]].Buffer;
						ASSERT(!buffer.IsValid() || (buffer != vertex && buffer != index && buffer != arguments && buffer != count),
							"Graphics binding table buffers cannot alias vertex, index, argument, or count buffers.");
						for (const VertexBufferBinding& extra : extraVertexBuffers)
						{
							ASSERT(!buffer.IsValid() || buffer != extra.Buffer, "Graphics binding table buffers cannot alias vertex, index, argument, or count buffers.");
						}
					}
				}
			}
			for (uint32 accelIndex = 0; accelIndex < set->AccelStructCount; ++accelIndex)
			{
				const AccelStructSlot* accel = findAccelStruct(set->AccelStructs[accelIndex]);
				ASSERT(accel != nullptr, "A binding set retains its acceleration structures.");
				ASSERT(accel->bBuilt && accel->Type == EAccelStructType::TopLevel, "Ray bindings require a built top-level acceleration structure.");
				if (accel->bUnavailable)
				{
					return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.RayQuery", "The acceleration structure is unavailable after GPU execution failure.");
				}
			}
			output.AccelCount += set->AccelStructCount;
		}
		if (pipeline.LayoutCount == 0)
		{
			ASSERT(sets.Size() <= 1, "The pipeline has no multiple binding layouts.");

			const BindingSetHandle set = singleBindingSet(sets);
			const Span<const TextureHandle> sampled = resolveGraphicsTextures(pipeline.Layout, set, Span<const TextureHandle>{});

			output.SampledCount = static_cast<uint32>(sampled.Size());
			Result<void> valid = validateGraphicsTextures(command, pipeline, listSlot, set, sampled, output.NativeSampled);

			if (!valid)
			{
				return valid;
			}

			for (uint32 binding = 0; binding < output.SampledCount; ++binding)
			{
				output.Sampled[binding] = sampled[binding];
			}

			const Span<const TextureHandle> storage = resolveGraphicsStorageTextures(pipeline.Layout, set, Span<const TextureHandle>{});

			output.StorageCount = static_cast<uint32>(storage.Size());
			valid = validateGraphicsStorageTextures(command, pipeline, listSlot, set, sampled, storage, output.NativeStorage);

			if (!valid)
			{
				return valid;
			}

			for (uint32 binding = 0; binding < output.StorageCount; ++binding)
			{
				output.Storage[binding] = storage[binding];
			}

			const Span<const BufferHandle> buffers = resolveGraphicsBuffers(pipeline.Layout, set);

			output.BufferCount = static_cast<uint32>(buffers.Size());
			valid = validateGraphicsBuffers(pipeline, buffers, set, vertex, index, arguments, output.NativeBuffers);

			if (!valid)
			{
				return valid;
			}

			for (uint32 binding = 0; binding < output.BufferCount; ++binding)
			{
				output.Buffers[binding] = buffers[binding];
			}
		}
		else
		{
			ASSERT(sets.Size() == pipeline.LayoutCount, "Multiple binding sets cannot mix with inline bindings.");

			uint32 samplerCount = 0;
			uint32 viewCount = 0;

			for (uint32 layoutIndex = 0; layoutIndex < pipeline.LayoutCount; ++layoutIndex)
			{
				BindingSetSlot* const set = findBindingSet(sets[layoutIndex]);

				ASSERT(set != nullptr, "RHI operations require a live handle owned by this device.");

				const BindingLayoutHandle handle = pipeline.Layouts[layoutIndex];
				const BindingLayoutSlot& layout = m_State->m_BindingLayouts[handle.Slot];

				ASSERT(set->Layout == handle && set->TextureCount == layout.GraphicsCount && set->StorageTextureCount == layout.GraphicsStorageCount && set->BufferCount == layout.GraphicsBufferCount,
					"A binding set differs from its graphics layout.");
				samplerCount += set->SamplerCount;
				viewCount += set->TextureCount + set->StorageTextureCount;
				GraphicsPipelineSlot part{};
				part.SampledTextureCount = layout.GraphicsCount;
				part.StorageTextureCount = layout.GraphicsStorageCount;
				part.BufferBindingCount = layout.GraphicsBufferCount;

				for (uint32 binding = 0; binding < layout.GraphicsCount; ++binding)
				{
					part.Samplers[binding] = layout.GraphicsTexture(binding).Sampler;
				}

				for (uint32 binding = 0; binding < layout.GraphicsBufferCount; ++binding)
				{
					part.BufferBindings[binding] = layout.GraphicsBuffer(binding);
				}

				const Span<const TextureHandle> sampled{ set->SampledTextures, set->TextureCount };
				const Span<const TextureHandle> storage{ set->StorageTextures, set->StorageTextureCount };
				const Span<const BufferHandle> buffers{ set->Buffers, set->BufferCount };
				Result<void> valid = validateGraphicsTextures(command, part, listSlot, sets[layoutIndex], sampled, output.NativeSampled + output.SampledCount);

				if (!valid)
				{
					return valid;
				}
				valid = validateGraphicsStorageTextures(command, part, listSlot, sets[layoutIndex], sampled, storage, output.NativeStorage + output.StorageCount);

				if (!valid)
				{
					return valid;
				}
				valid = validateGraphicsBuffers(part, buffers, sets[layoutIndex], vertex, index, arguments, output.NativeBuffers + output.BufferCount);

				if (!valid)
				{
					return valid;
				}

				for (const TextureHandle texture : sampled)
				{
					output.Sampled[output.SampledCount++] = texture;
				}

				for (const TextureHandle texture : storage)
				{
					output.Storage[output.StorageCount++] = texture;
				}

				for (const BufferHandle buffer : buffers)
				{
					output.Buffers[output.BufferCount++] = buffer;
				}
			}

			if (!command.SamplerUses.Reserve(command.SamplerUses.Size() + samplerCount) || !command.TextureViewUses.Reserve(command.TextureViewUses.Size() + viewCount))
			{
				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain multiple graphics samplers or views.");
			}

			for (uint32 current = 0; current < output.StorageCount; ++current)
			{
				for (uint32 sampled = 0; sampled < output.SampledCount; ++sampled)
				{
					ASSERT(output.Storage[current] != output.Sampled[sampled], "A texture is sampled and stored across layouts.");
				}

				for (uint32 previous = 0; previous < current; ++previous)
				{
					ASSERT(output.Storage[current] != output.Storage[previous], "A storage texture is repeated across layouts.");
				}
			}

			for (uint32 current = 0; current < output.BufferCount; ++current)
			{
				for (uint32 previous = 0; previous < current; ++previous)
				{
					if (output.Buffers[current] == output.Buffers[previous])
					{
						const NativeGraphicsBuffer& a = output.NativeBuffers[current];
						const NativeGraphicsBuffer& b = output.NativeBuffers[previous];

						ASSERT(a.Type == b.Type &&
							canShareBuffer(a.Type != EGraphicsBufferBindingType::StorageBuffer, b.Type != EGraphicsBufferBindingType::StorageBuffer, { a.ByteOffset, a.ByteCount }, { b.ByteOffset, b.ByteCount }),
							"A buffer aliases another graphics layout.");
					}
				}
			}
		}

		for (uint32 binding = 0; binding < output.BufferCount; ++binding)
		{
			ASSERT(output.Buffers[binding] != count, "The count buffer is also bound to a graphics shader.");
		}

		return Result<void>::Success();
	}

	void Device::trackGraphicsBindingSets(CommandSlot& command, Span<const BindingSetHandle> sets) noexcept
	{
		for (BindingSetHandle set : sets)
		{
			trackBoundSamplers(command, set);
			trackBoundViews(command, set);
			const BindingSetSlot* const bindingSet = findBindingSet(set);
			for (uint32 index = 0; index < bindingSet->AccelStructCount; ++index)
			{
				const AccelStructHandle handle = bindingSet->AccelStructs[index];
				trackAccelStruct(command, handle, findAccelStruct(handle));
			}
		}
	}

	[[nodiscard]] Result<void> Device::validateGraphicsTextures(
		CommandSlot& command,
		const GraphicsPipelineSlot& pipeline,
		uint32 listSlot,
		BindingSetHandle bindingSet,
		Span<const TextureHandle> handles,
		NativeSampledTexture* native) noexcept
	{
		BindingSetSlot* const set = bindingSet.IsValid() ? findBindingSet(bindingSet) : nullptr;

		if (set != nullptr && set->SamplerCount != 0 && !command.SamplerUses.Reserve(command.SamplerUses.Size() + set->SamplerCount))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain recorded samplers.");
		}

		if (set != nullptr && !command.TextureViewUses.Reserve(command.TextureViewUses.Size() + set->TextureCount))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain recorded texture views.");
		}

		ASSERT(handles.Size() == pipeline.SampledTextureCount, "The sampled texture count does not match the pipeline.");

		const bool bRanges = set != nullptr && m_State->m_BindingLayouts[set->Layout.Slot].RangeCount != 0;
		for (usize index = 0; index < handles.Size(); ++index)
		{
			SamplerSlot* const sampler = set != nullptr && set->SamplerCount != 0 && m_State->m_BindingLayouts[set->Layout.Slot].RangeCount == 0 ? findSampler(set->Samplers[index]) : nullptr;
			const SamplerDesc& samplerDesc = sampler != nullptr ? sampler->Descriptor : pipeline.Samplers[index];
			TextureSlot* const texture = findTexture(handles[index]);

			ASSERT(texture != nullptr, "RHI operations require valid handles for this device.");
			bool bTargetAlias = false;

			for (uint32 color = 0; color < command.ColorCount; ++color)
			{
				bTargetAlias |= handles[index] == command.ColorTargets[color];
			}

			const ETextureAspect aspect = set != nullptr && set->SampledViews[index].IsValid() ? m_State->m_TextureViews[set->SampledViews[index].Slot].Descriptor.Aspect : ETextureAspect::Depth;
			const bool bReadOnly = aspect == ETextureAspect::Stencil ? command.bReadOnlyStencil : command.bReadOnlyDepth;
			ASSERT(!bTargetAlias &&
				(handles[index] != command.DepthTarget || bReadOnly) &&
				HasUsage(texture->Descriptor.Usage, ETextureUsage::ShaderRead),
				"The sampled texture is an active attachment or lacks ShaderRead usage.");

			ASSERT(bRanges || !samplerDesc.bCompareEnabled || IsDepthFormat(texture->Descriptor.Format), "Comparison sampling requires a depth texture.");

			if (!canReadTexture(*texture, listSlot))
			{
				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list references the sampled texture.");
			}

			const EFormatSupport support = backend::QueryFormatSupport(m_State->m_Backend.State, sampledViewFormat(set, index, texture->Descriptor.Format));

			if (!HasFormatSupport(support, EFormatSupport::ShaderLoad) ||
				(!bRanges &&
				 (samplerDesc.MinFilter == ESamplerFilter::Linear || samplerDesc.MagFilter == ESamplerFilter::Linear || samplerDesc.MaxAnisotropy > 1.0f) &&
				 !HasFormatSupport(support, EFormatSupport::ShaderSample)) ||
				(!bRanges && samplerDesc.Reduction != ESamplerReduction::Standard && !HasFormatSupport(support, EFormatSupport::SamplerMinMax)))
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.TextureBinding", "The texture format does not support the sampler.");
			}
			native[index] = { texture->Native, sampler != nullptr ? sampler->Native : nullptr,
							 set != nullptr && set->SampledViews[index].IsValid() ? m_State->m_TextureViews[set->SampledViews[index].Slot].Native
																				  : nullptr };
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::validateGraphicsStorageTextures(
		CommandSlot& command,
		const GraphicsPipelineSlot& pipeline,
		uint32 listSlot,
		BindingSetHandle bindingSet,
		Span<const TextureHandle> sampled,
		Span<const TextureHandle> handles,
		NativeStorageTexture* native) noexcept
	{
		ASSERT(handles.Size() == pipeline.StorageTextureCount, "The storage texture count does not match the pipeline.");
		BindingSetSlot* const set = bindingSet.IsValid() ? findBindingSet(bindingSet) : nullptr;

		if (set != nullptr && !command.TextureViewUses.Reserve(command.TextureViewUses.Size() + set->TextureCount + set->StorageTextureCount))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain graphics storage texture views.");
		}

		for (usize index = 0; index < handles.Size(); ++index)
		{
			TextureSlot* const texture = findTexture(handles[index]);

			ASSERT(texture != nullptr, "RHI operations require valid handles for this device.");
			bool bAttachment = handles[index] == command.DepthTarget;

			for (uint32 color = 0; color < command.ColorCount; ++color)
			{
				bAttachment |= handles[index] == command.ColorTargets[color];
			}

			ASSERT(!bAttachment && (HasUsage(texture->Descriptor.Usage, ETextureUsage::Storage) || texture->bSamplerFeedback), "A storage texture is an attachment or lacks Storage usage.");

			if (texture->bSamplerFeedback)
			{
				bool bPaired = false;

				for (const TextureHandle sampledHandle : sampled)
				{
					bPaired |= sampledHandle == texture->PairedTexture;
				}

				ASSERT(bPaired, "The paired texture must be sampled in the same graphics layout.");
			}

			if (texture->RecordingSlot != UINT32_MAX && texture->RecordingSlot != listSlot)
			{
				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list references the storage texture.");
			}

			for (const TextureHandle sampledHandle : sampled)
			{
				ASSERT(sampledHandle != handles[index], "A texture is sampled and stored in the same draw.");
			}

			for (usize previous = 0; previous < index; ++previous)
			{
				ASSERT(handles[previous] != handles[index], "A storage texture is bound more than once.");
			}
			native[index] = { texture->Native, set != nullptr && set->StorageViews[index].IsValid()
												  ? m_State->m_TextureViews[set->StorageViews[index].Slot].Native
												  : nullptr };
		}

		return Result<void>::Success();
	}

	void Device::trackGraphicsTextures(CommandSlot& command, uint32 listSlot, Span<const TextureHandle> handles, ETrackedResourceState state) noexcept
	{
		for (const TextureHandle handle : handles)
		{
			TextureSlot& texture = m_State->m_Textures[handle.Slot];
			trackTextureReadOrWrite(command, listSlot, handle, texture, state);
		}
	}

	void Device::trackBoundSamplers(CommandSlot& command, BindingSetHandle bindingSet) noexcept
	{
		if (!bindingSet.IsValid())
		{
			return;
		}
		BindingSetSlot* const set = findBindingSet(bindingSet);

		if (set == nullptr)
		{
			return;
		}

		for (uint32 index = 0; index < set->SamplerCount; ++index)
		{
			(void)command.SamplerUses.Add(set->Samplers[index]);
			++m_State->m_Samplers[set->Samplers[index].Slot].RecordedUses;
		}
	}

	void Device::trackBoundViews(CommandSlot& command, BindingSetHandle bindingSet) noexcept
	{
		if (!bindingSet.IsValid())
		{
			return;
		}
		BindingSetSlot* const set = findBindingSet(bindingSet);

		if (set == nullptr)
		{
			return;
		}

		for (uint32 index = 0; index < set->TextureCount; ++index)
		{
			const TextureViewHandle view = set->SampledViews[index];

			if (!view.IsValid())
			{
				continue;
			}
			(void)command.TextureViewUses.Add(view);
			++m_State->m_TextureViews[view.Slot].RecordedUses;
		}

		for (uint32 index = 0; index < set->StorageTextureCount; ++index)
		{
			const TextureViewHandle view = set->StorageViews[index];

			if (!view.IsValid())
			{
				continue;
			}
			(void)command.TextureViewUses.Add(view);
			++m_State->m_TextureViews[view.Slot].RecordedUses;
		}
	}
}
