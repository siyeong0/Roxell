#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<RayTracingPipelineHandle> Device::CreateRayTracingPipeline(const RayTracingPipelineDesc& desc)
	{
		CallScope call(*this, false);

		ASSERT(!(desc.Shaders.IsEmpty() ||
				desc.Shaders.Size() > 32 ||
				desc.HitGroups.Size() > 32 ||
				desc.GlobalLayouts.Size() > 4 ||
				desc.MaxRecursionDepth == 0 ||
				desc.PushConstantBytes > 128 ||
				desc.PushConstantBytes % 4 != 0),
			"The ray pipeline description is invalid.");

		if (desc.bAllowOpacityMicromaps && !m_State->m_Backend.bOpacityMicromap)
		{
			return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RayPipeline", "Opacity micromaps are unavailable.");
		}

		if (desc.bAllowClusterAccelerationStructures && !m_State->m_Backend.bRayTracingClusters)
		{
			return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RayPipeline", "Cluster acceleration structures are unavailable.");
		}

		for (BindingLayoutHandle handle : desc.GlobalLayouts)
		{
			ASSERT(!(findBindingLayout(handle) == nullptr), "RHI operations require valid handles for this device.");
		}
		NativeRayShaderDesc shaders[128]{};
		NativeRayHitGroupDesc groups[32]{};
		BindingLayoutScratch shaderLocalScratch[128]{};
		BindingLayoutScratch groupLocalScratch[32]{};
		const auto normalizeLocal = [](const BindingLayoutSlot& layout, BindingLayoutScratch& scratch,
			NativeBindingLayoutDesc& native) ->
			Result<void>
			{
				if (layout.Kind != EBindingLayoutKind::RayTracing || layout.Space != 4 || layout.RangeSamplerCount != layout.ComputeSampledCount || layout.RangeAccelStructCount != 0)
				{
					return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.LocalRoot", "The local resource layout is not representable by the single local shader record ABI.");
				}
				uint32 sampledBindings[8]{}; uint32 samplerBindings[8]{}; uint32 sampledCount = 0; uint32 samplerCount = 0;
				for (uint32 index = 0; index < layout.RangeCount; ++index)
				{
					const BindingRangeDesc range = layout.Range(index);
					if (range.Source != EBindingRangeSource::Fixed || range.DescriptorCount != 1)
					{
						return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.LocalRoot", "Single local resource records require individual fixed ranges.");
					}
					if (range.Type == EDescriptorType::SampledTexture)
					{
						sampledBindings[sampledCount++] = range.Binding;
					}
					else if (range.Type == EDescriptorType::Sampler)
					{
						samplerBindings[samplerCount++] = range.Binding;
					}
				}
				for (uint32 index = 0; index < sampledCount; ++index)
				{
					if (sampledBindings[index] != samplerBindings[index])
					{
						return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.LocalRoot", "Single local samplers must match sampled texture registers in order.");
					}
				}
				scratch.Capture(layout);
				for (uint32 index = 0; index < layout.ComputeTextureCount; ++index)
				{
					ComputeTextureBindingDesc& texture = scratch.ComputeTextures[index];
					if (texture.Type != EComputeTextureBindingType::SampledTexture)
					{
						continue;
					}
					for (uint32 sampler = 0; sampler < layout.RangeCount; ++sampler)
					{
						const BindingRangeDesc range = layout.Range(sampler);
						if (range.Type == EDescriptorType::Sampler && range.Binding == texture.Slot && range.ImmutableSamplerOrNull != nullptr)
						{
							texture.Sampler = *range.ImmutableSamplerOrNull;
							break;
						}
					}
				}
				native.Buffers = { scratch.ComputeBuffers, layout.ComputeCount };
				native.Textures = { scratch.ComputeTextures, layout.ComputeTextureCount };

				return Result<void>::Success();
			};
		Array<RayLocalRootRecipe> localRecipes(m_State->m_Resource);
		if (!localRecipes.Reserve(desc.Shaders.Size() + desc.HitGroups.Size()))
		{
			return Result<RayTracingPipelineHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not capture local root recipes.");
		}
		bool bTypedRoots = false;
		bool bSimpleLocalRecords = false;
		auto captureLocalRecipe = [&](Span<const BindingLayoutHandle> handles,
			Span<const RayLocalRootArgumentDesc> arguments,
			BindingLayoutHandle singleLayout, uint32 rawBytes) -> Result<void> {
				using R = Result<void>;
				bTypedRoots |= !arguments.IsEmpty();
				bSimpleLocalRecords |= singleLayout.IsValid() || rawBytes != 0;
				if ((!arguments.IsEmpty() || !handles.IsEmpty()) && (m_State->m_Kind != EBackend::D3D12 || singleLayout.IsValid() || rawBytes != 0))
				{
					return R::Failure(EErrorCode::Unsupported, "Rhi.LocalRoot", "Typed D3D12 roots cannot mix with a single-layout or byte record.");
				}
				ASSERT(handles.Size() <= 4, "The local layout count exceeds its bound.");
				NativeBindingLayoutDesc native[4]{
				};
				for (
					uint32 index = 0; index < handles.Size(); ++index)
				{
					BindingLayoutSlot* layout = findBindingLayout(handles[index]);

					ASSERT(layout != nullptr, "RHI operations require valid handles for this device.");
					ASSERT(layout->Kind == EBindingLayoutKind::RayTracing && layout->RangeCount != 0, "Typed local roots require fixed ray descriptor layouts.");
					native[index].Ranges = { layout->Entries, layout->RangeCount };
					native[index].Space = layout->Space;
					native[
						index].bRangeLayout = true;
				}

				Result<RayLocalRootRecipe> recipe = RayLocalRootRecipe::Create(handles, arguments, { native, handles.Size() }, m_State->m_Resource);
				if (!recipe)
				{
					ASSERT(recipe.GetError().Code != EErrorCode::InvalidArgument, "The ordered local root recipe does not match its fixed layouts.");

					return R::Failure(recipe.GetError());
				}

				localRecipes.AddRequired(Move(recipe).TakeValue());

				return R::Success();
			};
		for (const RayTracingShaderDesc& entry : desc.Shaders)
		{
			Result<void> recipe = captureLocalRecipe(entry.LocalLayouts, entry.LocalRootArguments, entry.LocalLayout, entry.LocalDataBytes);

			if (!recipe)
			{
				return Result<RayTracingPipelineHandle>::Failure(recipe.GetError());
			}
		}
		for (const RayTracingHitGroupDesc& group : desc.HitGroups)
		{
			Result<void> recipe = captureLocalRecipe(group.LocalLayouts, group.LocalRootArguments, group.LocalLayout, group.LocalDataBytes);

			if (!recipe)
			{
				return Result<RayTracingPipelineHandle>::Failure(recipe.GetError());
			}
		}
		if (bTypedRoots && bSimpleLocalRecords)
		{
			return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.LocalRoot", "One pipeline cannot mix typed roots and single-layout or byte records.");
		}
		if (bTypedRoots)
		{
			for (const BindingLayoutHandle handle : desc.GlobalLayouts)
			{
				const BindingLayoutSlot& layout = m_State->m_BindingLayouts[handle.Slot];
				if (layout.Kind != EBindingLayoutKind::RayTracing || layout.RangeCount == 0)
				{
					return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.LocalRoot", "Typed roots require fixed ray global layouts.");
				}
			}
		}
		uint32 shaderCount = 0;
		uint32 rayGenerationCount = 0;
		auto appendShader = [&](ShaderHandle handle, EShaderStage expected, uint32& index) -> Result<void>
			{
				if (handle == ShaderHandle{})
				{
					return Result<void>::Success();
				}
				ShaderSlot* const shader = findShader(handle);

				ASSERT(shader != nullptr, "RHI operations require valid handles for this device.");

				ASSERT(expected == shader->Stage, "The shader stage does not match the ray group.");
				const Array<Byte>& bytecode = shader->Library.IsValid() ? m_State->m_ShaderLibraries[shader->Library.Slot].Entries[shader->LibraryEntry].Bytecode : shader->Bytecode;
				index = shaderCount;
				shaders[shaderCount++] = { {bytecode.Data(), bytecode.Size()}, shader->EntryPoint, shader->Stage };

				return Result<void>::Success();
			};
		for (const RayTracingShaderDesc& entry : desc.Shaders)
		{
			ShaderSlot* const shader = findShader(entry.Shader);

			ASSERT(shader != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(!(shader->Stage != EShaderStage::RayGeneration && shader->Stage != EShaderStage::Miss && shader->Stage != EShaderStage::Callable), "A general ray shader stage is invalid.");

			ASSERT(!(entry.LocalDataBytes > 128 || entry.LocalDataBytes % 4 != 0), "Local constants must be whole words, at most 128 bytes.");

			ASSERT(!(entry.LocalLayout.IsValid() && findBindingLayout(entry.LocalLayout) == nullptr), "RHI operations require valid handles for this device.");

			uint32 index = 0xFFFFFFFFu;
			Result<void> resolved = appendShader(entry.Shader, shader->Stage, index);

			if (!resolved)
			{
				return Result<RayTracingPipelineHandle>::Failure(resolved.GetError());
			}
			shaders[index].LocalDataBytes = entry.LocalDataBytes;
			ASSERT(!(entry.LocalDataAlignmentBytes == 0 || entry.LocalDataAlignmentBytes > 128 || (entry.LocalDataAlignmentBytes & (entry.LocalDataAlignmentBytes - 1)) != 0),
				"The local data alignment is invalid.");
			if (m_State->m_Kind == EBackend::D3D12 && entry.LocalDataBytes != 0 && 32 % entry.LocalDataAlignmentBytes != 0)
			{
				return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RayPipeline", "The native shader identifier cannot align local data.");
			}
			shaders[index].LocalDataAlignmentBytes = entry.LocalDataAlignmentBytes;
			shaders[index].LocalRootArguments = localRecipes[&entry - desc.Shaders.Data()].GetArguments();

			if (entry.LocalLayout.IsValid())
			{
				BindingLayoutSlot* const layout = findBindingLayout(entry.LocalLayout);
				Result<void> normalized = normalizeLocal(*layout, shaderLocalScratch[index], shaders[index].LocalLayout);
				if (!normalized)
				{
					return Result<RayTracingPipelineHandle>::Failure(normalized.GetError());
				}
			}

			if (shader->Stage == EShaderStage::RayGeneration)
			{
				++rayGenerationCount;
			}
		}

		ASSERT(!(rayGenerationCount == 0), "A ray generation shader is required.");

		for (usize groupIndex = 0; groupIndex < desc.HitGroups.Size(); ++groupIndex)
		{
			const RayTracingHitGroupDesc& source = desc.HitGroups[groupIndex];

			ASSERT(!(source.LocalLayout.IsValid() && findBindingLayout(source.LocalLayout) == nullptr), "RHI operations require valid handles for this device.");

			ASSERT(!((source.ClosestHitShader == ShaderHandle{} && source.AnyHitShader == ShaderHandle{} && source.IntersectionShader == ShaderHandle{}) ||
					(source.bProcedural != source.IntersectionShader.IsValid())), "The hit group is invalid.");
			NativeRayHitGroupDesc& group = groups[groupIndex];
			group.bProcedural = source.bProcedural;
			ASSERT(!(source.LocalDataBytes > 128 || source.LocalDataBytes % 4 != 0), "Local constants must be whole words, at most 128 bytes.");
			group.LocalDataBytes = source.LocalDataBytes;
			ASSERT(!(source.LocalDataAlignmentBytes == 0 || source.LocalDataAlignmentBytes > 128 || (source.LocalDataAlignmentBytes & (source.LocalDataAlignmentBytes - 1)) != 0), "The local data alignment is invalid.");

			if (m_State->m_Kind == EBackend::D3D12 && source.LocalDataBytes != 0 && 32 % source.LocalDataAlignmentBytes != 0)
			{
				return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RayPipeline", "The native shader identifier cannot align local data.");
			}
			group.LocalDataAlignmentBytes = source.LocalDataAlignmentBytes;
			group.LocalRootArguments = localRecipes[desc.Shaders.Size() + groupIndex].GetArguments();

			if (source.LocalLayout.IsValid())
			{
				BindingLayoutSlot*
					const layout = findBindingLayout(source.LocalLayout);
				Result<void> normalized = normalizeLocal(*layout, groupLocalScratch[groupIndex], group.LocalLayout);
				if (!normalized)
				{
					return Result<RayTracingPipelineHandle>::Failure(normalized.GetError());
				}
			}

			Result<void> closest = appendShader(source.ClosestHitShader, EShaderStage::ClosestHit, group.ClosestHitIndex);

			if (!closest)
			{
				return Result<RayTracingPipelineHandle>::Failure(closest.GetError());
			}

			Result<void> any = appendShader(source.AnyHitShader, EShaderStage::AnyHit, group.AnyHitIndex);

			if (!any)
			{
				return Result <RayTracingPipelineHandle>::Failure(any.GetError());
			}

			Result<void> intersection = appendShader(source.IntersectionShader, EShaderStage::Intersection, group.IntersectionIndex);

			if (!intersection)
			{
				return Result<RayTracingPipelineHandle>::Failure(intersection.GetError());
			}
		}

		Result<void> rangeLayouts = validateRangePipelineLayouts(desc.GlobalLayouts, EBindingLayoutKind::RayTracing, false);
		if (!rangeLayouts)
		{
			return Result<RayTracingPipelineHandle>::Failure(rangeLayouts.GetError());
		}
		const bool bRanges = !desc.GlobalLayouts.IsEmpty();
		NativeBindingLayoutDesc nativeLayouts[4]{
		};

		for (usize layoutIndex = 0; layoutIndex < desc.GlobalLayouts.Size(); ++layoutIndex)
		{
			BindingLayoutSlot* const globalLayout = findBindingLayout(desc.GlobalLayouts[layoutIndex]);
			ASSERT(globalLayout != nullptr, "A validated global layout must exist.");
			if (m_State->m_Kind == EBackend::Vulkan && globalLayout->Space >= 4)
			{
				return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RayPipeline", "Vulkan reserves space 4 for local ray records.");
			}
			uint32 repeated = 1;

			for (usize previous = 0; previous < layoutIndex; ++previous)
			{
				if (desc.GlobalLayouts[previous] == desc.GlobalLayouts[layoutIndex])
				{
					++repeated;
				}
			}
			if ((!bRanges &&
				 ((globalLayout->ComputeCount == 0 && globalLayout->ComputeTextureCount == 0) ||
				  globalLayout->GraphicsCount != 0 ||
				  globalLayout->GraphicsStorageCount != 0 ||
				  globalLayout->GraphicsBufferCount != 0)) ||
				globalLayout->References > NumericLimits<uint32>::max() - repeated)
			{
				return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RayPipeline", "The global ray layout requires compute resources.");
			}
			if (bRanges)
			{
				nativeLayouts[layoutIndex].Ranges = { globalLayout->Entries, globalLayout->RangeCount };
				nativeLayouts[layoutIndex].Space = globalLayout->Space;
				nativeLayouts[layoutIndex].bRangeLayout = true;

				for (uint32 rangeIndex = 0; rangeIndex < nativeLayouts[layoutIndex].Ranges.Size(); ++rangeIndex)
				{
					const BindingRangeDesc range = nativeLayouts[layoutIndex].Range(rangeIndex);
					ASSERT(!(m_State->m_Kind == EBackend::D3D12 && desc.PushConstantBytes != 0 && globalLayout->Space == 5 && range.Type == EDescriptorType::UniformBuffer && range.Binding == 0), "Push constants overlap a global register.");
				}
			}
			else
			{
				nativeLayouts[layoutIndex] = {};
				nativeLayouts[layoutIndex].ScalarEntries = { globalLayout->Entries, globalLayout->ComputeCount + globalLayout->ComputeTextureCount };
				nativeLayouts[layoutIndex].ScalarBufferCount = globalLayout->ComputeCount;
			}
		}

		BindingLayoutHandle localHandles[64]{};
		uint32 localCount = 0;

		for (const RayTracingShaderDesc& shader : desc.Shaders)
		{
			if (shader.LocalLayout.IsValid())
			{
				localHandles[localCount++] = shader.LocalLayout;
			}
		}

		for (const RayTracingHitGroupDesc& group : desc.HitGroups)
		{
			if (group.LocalLayout.IsValid())
			{
				localHandles[localCount++] = group.LocalLayout;
			}
		}

		for (uint32 index = 0; index < localCount; ++index)
		{
			BindingLayoutSlot* const layout = findBindingLayout(localHandles[index]);
			uint32 references = 1;

			for (uint32 previous = 0; previous < index; ++previous)
			{
				if (localHandles[previous] == localHandles[index])
				{
					++references;
				}
			}

			if ((layout->ComputeCount == 0 && layout->ComputeTextureCount == 0) || layout->References > NumericLimits<uint32>::max() - references)
			{
				return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RayPipeline", "Local ray layouts require compute resources.");
			}
		}
		BindingLayoutHandle allLayouts[324]{};
		uint32 allLayoutCount = 0;

		for (BindingLayoutHandle handle : desc.GlobalLayouts)
		{
			allLayouts[allLayoutCount++] = handle;
		}

		for (uint32 index = 0; index < localCount; ++index)
		{
			allLayouts[allLayoutCount++] = localHandles[index];
		}
		for (const RayLocalRootRecipe& recipe : localRecipes)
		{
			for (const BindingLayoutHandle handle : recipe.GetLayouts())
			{
				allLayouts[allLayoutCount++] = handle;
			}
		}

		for (uint32 index = 0; index < allLayoutCount; ++index)
		{
			uint32 references = 0;

			for (uint32 other = 0; other < allLayoutCount; ++other)
			{
				if (allLayouts[other] == allLayouts[index])
				{
					++references;
				}
			}

			if (m_State->m_BindingLayouts[allLayouts[index].Slot].References >
				NumericLimits<uint32>::max() - references)
			{
				return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Conflict, "Rhi.RayPipeline", "The binding layout reference count is exhausted.");
			}
		}

		if constexpr (backend::HAS_CREATE_RAY_TRACING_PIPELINE == false)
		{
			return Result<RayTracingPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RayPipeline", "Ray pipeline creation is unavailable.");
		}

		const NativeRayPipelineDesc native{ {shaders, shaderCount},
										   {groups, desc.HitGroups.Size()},
										   {nativeLayouts, desc.GlobalLayouts.Size()},
										   static_cast<uint32>(desc.Shaders.Size()),
										   desc.MaxPayloadBytes,
										   desc.MaxAttributeBytes,
										   desc.MaxRecursionDepth,
										   desc.PushConstantBytes,
										   0xFFFFFFFFu,
										   desc.bAllowOpacityMicromaps,
										   desc.bAllowClusterAccelerationStructures,
										   desc.LocalDescriptorCapacity };
		Result<void*> created = backend::CreateRayTracingPipeline(m_State->m_Backend.State, native);

		if (!created)
		{
			return Result<RayTracingPipelineHandle>::Failure(created.GetError());
		}

		const bool bNewSlot = m_State->m_FreeRayTracingPipeline == 0xFFFFFFFFu;
		const uint32 slotIndex = bNewSlot ? static_cast<uint32>(m_State->m_RayTracingPipelines.Size()) : m_State->m_FreeRayTracingPipeline;

		if (bNewSlot && (m_State->m_RayTracingPipelines.Size() >= 0xFFFFFFFFu || !m_State->m_RayTracingPipelines.Add(RayTracingPipelineSlot(m_State->m_Resource))))
		{
			backend::DestroyRayTracingPipelineNow(m_State->m_Backend.State, created.Value());

			return Result<RayTracingPipelineHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a ray pipeline slot.");
		}
		RayTracingPipelineSlot& slot = m_State->m_RayTracingPipelines[slotIndex];

		if (!bNewSlot)
		{
			m_State->m_FreeRayTracingPipeline = slot.NextFree;
		}
		slot.Native = created.Value();
		slot.LocalRootRecipes = Move(localRecipes);
		slot.bTypedLocalRoots = bTypedRoots;
		for (rhi::RayLocalRootRecipe const& recipe : slot.LocalRootRecipes)
		{
			for (const rhi::BindingLayoutHandle handle : recipe.GetLayouts())
			{
				++m_State->m_BindingLayouts[handle.Slot].References;
			}
		}

		slot.LayoutCount = static_cast<uint32>(desc.GlobalLayouts.Size());
		slot.AccelStructSlot = 0xFFFFFFFFu;
		slot.PushConstantBytes = desc.PushConstantBytes;

		for (uint32 layoutIndex = 0; layoutIndex < slot.LayoutCount; ++layoutIndex)
		{
			BindingLayoutSlot& layout = m_State->m_BindingLayouts[desc.GlobalLayouts[layoutIndex].Slot];
			RayTracingPipelineSlot::GlobalLayout& target = slot.Layouts[layoutIndex];
			target.Handle = desc.GlobalLayouts[layoutIndex];
			target.GlobalBufferCount = layout.ComputeCount;
			target.TextureCount = layout.ComputeTextureCount;
			target.SampledTextureCount = layout.ComputeSampledCount;
			target.StorageTextureCount = layout.ComputeStorageCount;

			for (uint32 binding = 0; binding < target.GlobalBufferCount; ++binding)
			{
				target.GlobalBuffers[binding] = layout.ComputeBuffer(binding);
			}

			for (uint32 binding = 0; binding < target.TextureCount; ++binding)
			{
				target.Textures[binding] = layout.ComputeTexture(binding);
			}
			++layout.References;
		}

		for (uint32 index = 0; index < native.GeneralShaderCount; ++index)
		{
			slot.GeneralLocalLayouts[index] = desc.Shaders[index].LocalLayout;
			slot.GeneralLocalDataBytes[index] = bTypedRoots ? slot.LocalRootRecipes[index].GetByteSize() : desc.Shaders[index].LocalDataBytes;

			if (slot.GeneralLocalLayouts[index].IsValid())
			{
				++m_State->m_BindingLayouts[slot.GeneralLocalLayouts[index].Slot].References;
			}
		}

		for (uint32 index = 0; index < desc.HitGroups.Size(); ++index)
		{
			slot.HitLocalLayouts[index] = desc.HitGroups[index].LocalLayout;
			slot.HitLocalDataBytes[index] = bTypedRoots ? slot.LocalRootRecipes[native.GeneralShaderCount + index].GetByteSize() : desc.HitGroups[index].LocalDataBytes;

			if (slot.HitLocalLayouts[index].IsValid())
			{
				++m_State->m_BindingLayouts[slot.HitLocalLayouts[index].Slot].References;
			}
		}

		for (uint32 index = 0; index < native.GeneralShaderCount; ++index)
		{
			slot.GeneralStages[index] = shaders[index].Stage;
		}
		slot.GeneralShaderCount = native.GeneralShaderCount;
		slot.HitGroupCount = static_cast<uint32>(native.HitGroups.Size());
		slot.ShaderTableReferences = 0;
		slot.RecordedUses = 0;
		slot.PreparedReferences = 0;
		slot.NextFree = 0xFFFFFFFFu;
		slot.bLive = true;

		return Result<RayTracingPipelineHandle>::Success({ m_State->m_Owner, slotIndex, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyRayTracingPipeline(RayTracingPipelineHandle handle)
	{
		CallScope call(*this, false);
		RayTracingPipelineSlot* const slot = findRayTracingPipeline(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->ShaderTableReferences != 0 || slot->RecordedUses != 0 || slot->PreparedReferences != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RayPipeline", "Shader tables or commands still reference the ray pipeline.");
		}

		Result<void> destroyed = backend::DestroyRayTracingPipeline(m_State->m_Backend.State, slot->Native);

		if (!destroyed)
		{
			return destroyed;
		}

		for (uint32 index = 0; index < slot->LayoutCount; ++index)
		{
			--m_State->m_BindingLayouts[slot->Layouts[index].Handle.Slot].References;
			slot->Layouts[index] = {};
		}
		slot->LayoutCount = 0;

		for (uint32 index = 0; index < slot->GeneralShaderCount; ++index)
		{
			if (slot->GeneralLocalLayouts[index].IsValid())
			{
				--m_State->m_BindingLayouts[slot->GeneralLocalLayouts[index].Slot].References;
				slot->GeneralLocalLayouts[index] = {};
			}
		}

		for (uint32 index = 0; index < slot->HitGroupCount; ++index)
		{
			if (slot->HitLocalLayouts[index].IsValid())
			{
				--m_State->m_BindingLayouts[slot->HitLocalLayouts[index].Slot].References;
				slot->HitLocalLayouts[index] = {};
			}
		}
		slot->AccelStructSlot = 0xFFFFFFFFu;
		for (rhi::RayLocalRootRecipe const& recipe : slot->LocalRootRecipes)
		{
			for (const rhi::BindingLayoutHandle layout : recipe.GetLayouts())
			{
				--m_State->m_BindingLayouts[layout.Slot].References;
			}
		}
		slot->LocalRootRecipes.Reset();
		slot->bTypedLocalRoots = false;
		slot->PushConstantBytes = 0;
		slot->Native = nullptr;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeRayTracingPipeline;
			m_State->m_FreeRayTracingPipeline = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<ShaderTableHandle> Device::CreateShaderTable(const ShaderTableDesc& desc)
	{
		CallScope call(*this, false);
		RayTracingPipelineSlot* const pipeline = findRayTracingPipeline(desc.Pipeline);

		ASSERT(pipeline != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(desc.MissShaderIndices.Size() <= 32 && desc.HitGroupIndices.Size() <= 32 && desc.CallableShaderIndices.Size() <= 32, "The shader table exceeds its capacity.");
		if (
			pipeline->ShaderTableReferences == NumericLimits<uint32>::max())
		{
			return Result<ShaderTableHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.ShaderTable", "The pipeline has too many shader table references.");
		}

		ASSERT(!(desc.RayGenerationShaderIndex >= pipeline->GeneralShaderCount || pipeline->GeneralStages[desc.RayGenerationShaderIndex] != EShaderStage::RayGeneration),
			"A ray generation shader is required.");

		for (uint32 index : desc.MissShaderIndices)
		{
			ASSERT(!(index >= pipeline->GeneralShaderCount || pipeline->GeneralStages[index] != EShaderStage::Miss), "A miss shader index is invalid.");
		}

		for (uint32 index : desc.HitGroupIndices)
		{
			ASSERT(!(index >= pipeline->HitGroupCount), "A hit group index is invalid.");
		}

		for (uint32 index : desc.CallableShaderIndices)
		{
			ASSERT(!(index >= pipeline->GeneralShaderCount || pipeline->GeneralStages[index] != EShaderStage::Callable), "A callable shader index is invalid.");
		}

		ASSERT(!((!desc.MissLocalSets.IsEmpty() && desc.MissLocalSets.Size() != desc.MissShaderIndices.Size()) ||
				(!desc.HitGroupLocalSets.IsEmpty() && desc.HitGroupLocalSets.Size() != desc.HitGroupIndices.Size()) ||
				(!desc.CallableLocalSets.IsEmpty() && desc.CallableLocalSets.Size() != desc.CallableShaderIndices.Size())),
			"Local set counts must match shader table entries.");

		ASSERT(!((!desc.MissLocalData.IsEmpty() && desc.MissLocalData.Size() != desc.MissShaderIndices.Size()) ||
				(!desc.HitGroupLocalData.IsEmpty() && desc.HitGroupLocalData.Size() != desc.HitGroupIndices.Size()) ||
				(!desc.CallableLocalData.IsEmpty() && desc.CallableLocalData.Size() != desc.CallableShaderIndices.Size())),
			"Local data arrays must match their record counts.");
		const bool bTyped = pipeline->bTypedLocalRoots;
		ASSERT(!((bTyped &&
				  (desc.RayGenerationLocalSet.IsValid() ||
				   !desc.MissLocalSets.IsEmpty() ||
				   !desc.HitGroupLocalSets.IsEmpty() ||
				   !desc.CallableLocalSets.IsEmpty() ||
				   !desc.RayGenerationLocalData.IsEmpty() ||
				   !desc.MissLocalData.IsEmpty() ||
				   !desc.HitGroupLocalData.IsEmpty() ||
				   !desc.CallableLocalData.IsEmpty())) ||
				(!bTyped &&
				 (!desc.RayGenerationRecord.LocalSets.IsEmpty() ||
				  !desc.RayGenerationRecord.Constants.IsEmpty() ||
				  !desc.MissRecords.IsEmpty() ||
				  !desc.HitGroupRecords.IsEmpty() ||
				  !desc.CallableRecords.IsEmpty())) ||
				(!desc.MissRecords.IsEmpty() && desc.MissRecords.Size() != desc.MissShaderIndices.Size()) ||
				(!desc.HitGroupRecords.IsEmpty() && desc.HitGroupRecords.Size() != desc.HitGroupIndices.Size()) ||
				(!desc.CallableRecords.IsEmpty() && desc.CallableRecords.Size() != desc.CallableShaderIndices.Size())),
			"Typed and single-layout or byte record descriptions disagree.");
		ShaderTableRecordDesc typedRecords[97]{};
		uint32 typedGroups[97]{};
		typedRecords[0] = desc.RayGenerationRecord;
		typedGroups[0] = desc.RayGenerationShaderIndex;
		uint32 typedCount = 1;
		for (uint32 index = 0; index < desc.MissShaderIndices.Size(); ++index)
		{
			typedRecords[typedCount] = desc.MissRecords.IsEmpty() ? ShaderTableRecordDesc{} : desc.MissRecords[index];
			typedGroups[typedCount++] = desc.MissShaderIndices[index];
		}
		for (uint32 index = 0; index < desc.HitGroupIndices.Size(); ++index)
		{
			typedRecords[typedCount] = desc.HitGroupRecords.IsEmpty() ? ShaderTableRecordDesc{} : desc.HitGroupRecords[index];
			typedGroups[typedCount++] = pipeline->GeneralShaderCount + desc.HitGroupIndices[index];
		}
		for (uint32 index = 0; index < desc.CallableShaderIndices.Size(); ++index)
		{
			typedRecords[typedCount] = desc.CallableRecords.IsEmpty() ? ShaderTableRecordDesc{} : desc.CallableRecords[index];
			typedGroups[typedCount++] = desc.CallableShaderIndices[index];
		}

		Array<BindingSetHandle> typedSets(m_State->m_Resource);
		uint32 typedSetOffsets[98]{};
		if (bTyped && !typedSets.Reserve(typedCount * 4))
		{
			return Result<ShaderTableHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not capture local binding set ownership.");
		}
		if (bTyped)
		{
			for (uint32 record = 0; record < typedCount; ++record)
			{
				rhi::RayLocalRootRecipe const& recipe = pipeline->LocalRootRecipes[typedGroups[record]];
				const ShaderTableRecordDesc& source = typedRecords[record];
				ASSERT(!(source.LocalSets.Size() != recipe.GetLayouts().Size()), "Typed local sets must match their ordered layouts.");
				for (uint32 index = 0; index < source.LocalSets.Size(); ++index)
				{
					const BindingSetHandle handle = source.LocalSets[index];
					BindingSetSlot* set = findBindingSet(handle);
					ASSERT(!(set == nullptr || set->Layout != recipe.GetLayouts()[index]), "RHI operations require valid handles for this device.");
					if (set->Tables)
					{
						return Result<ShaderTableHandle>::Failure(
							EErrorCode::Unsupported,
							"Rhi.ShaderTable",
							"Table-backed local root records are not represented by this record profile.");
					}
					uint32 references = 1;
					for (rhi::BindingSetHandle const previous : typedSets)
					{
						references += previous == handle ? 1u : 0u;
					}
					if (set->ShaderTableReferences > UINT32_MAX - references)
					{
						return Result<ShaderTableHandle>::Failure(EErrorCode::Conflict, "Rhi.ShaderTable", "The local set reference count is exhausted.");
					}
					typedSets.AddRequired(handle);
				}

				typedSetOffsets[record + 1] = static_cast<uint32>(typedSets.Size());
				uint32 constants = 0;
				for (const rhi::NativeRayLocalRootArgument& argument : recipe.GetArguments())
				{
					if (argument.Kind == ERayLocalRootArgumentKind::Constants32)
					{
						ASSERT(constants < source.Constants.Size() && source.Constants[constants].Size() == argument.ValueCount * 4, "Local root constants require their exact word extent.");
						++constants;
					}
				}
				ASSERT(!(constants != source.Constants.Size()), "Unexpected local root constant spans.");
			}
		}

		Span<const Byte> recordData[97]{};
		uint32 expectedData[97]{};
		uint32 recordCount = 1;
		recordData[0] = desc.RayGenerationLocalData;
		expectedData[0] = pipeline->GeneralLocalDataBytes[desc.RayGenerationShaderIndex];

		for (usize index = 0; index < desc.MissShaderIndices.Size(); ++index)
		{
			recordData[recordCount] = desc.MissLocalData.IsEmpty() ? Span<const Byte>{} : desc.MissLocalData[index];
			expectedData[recordCount++] = pipeline->GeneralLocalDataBytes[desc.MissShaderIndices[index]];
		}

		for (usize index = 0; index < desc.HitGroupIndices.Size(); ++index)
		{
			recordData[recordCount] = desc.HitGroupLocalData.IsEmpty() ? Span<const Byte>{} : desc.HitGroupLocalData[index];
			expectedData[recordCount++] = pipeline->HitLocalDataBytes[desc.HitGroupIndices[index]];
		}

		for (usize index = 0; index < desc.CallableShaderIndices.Size(); ++index)
		{
			recordData[recordCount] = desc.CallableLocalData.IsEmpty() ? Span<const Byte>{} : desc.CallableLocalData[index];
			expectedData[recordCount++] = pipeline->GeneralLocalDataBytes[desc.CallableShaderIndices[index]];
		}

		uint32 dataOffsets[98]{};
		for (uint32 index = 0; index < recordCount; ++index)
		{
			ASSERT(!(!bTyped && recordData[index].Size() != expectedData[index]), "Local data size differs from the pipeline declaration.");
			dataOffsets[index + 1] = dataOffsets[index] + expectedData[index];
		}

		Array<Byte> copiedData(m_State->m_Resource);

		if (!copiedData.ResizeUninitialized(dataOffsets[recordCount]))
		{
			return Result<ShaderTableHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not copy shader record data.");
		}

		for (uint32 index = 0; index < recordCount; ++index)
		{
			if (bTyped && expectedData[index] != 0)
			{
				Byte* destination = copiedData.Data() + dataOffsets[index];
				SetBytes(destination, 0, expectedData[index]);
				uint32 constants = 0;
				for (const rhi::NativeRayLocalRootArgument& argument : pipeline->LocalRootRecipes[typedGroups[index]].GetArguments())
				{
					if (argument.Kind == ERayLocalRootArgumentKind::Constants32)
					{
						const Span<const Byte> source = typedRecords[index].Constants[constants++];
						CopyBytes(destination + argument.ByteOffset, source.Data(), source.Size());
					}
				}
			}
			else if (!recordData[index].IsEmpty())
			{
				CopyBytes(copiedData.Data() + dataOffsets[index], recordData[index].Data(), recordData[index].Size());
			}
		}
		BindingSetHandle localSets[97]{};
		uint32 localCount = 0;
		auto checkLocal = [&](BindingLayoutHandle layoutHandle, BindingSetHandle setHandle) -> Result<void>
			{
				ASSERT(layoutHandle.IsValid() == setHandle.IsValid(), "A local set does not match its shader layout.");

				if (!setHandle.IsValid())
				{
					return Result<void>::Success();
				}
				BindingSetSlot* const set = findBindingSet(setHandle);

				ASSERT(set != nullptr, "RHI operations require valid handles for this device.");

				ASSERT(set->Layout == layoutHandle, "The local set uses another layout.");

				uint32 references = 1;

				for (uint32 previous = 0; previous < localCount; ++previous)
				{
					if (localSets[previous] == setHandle)
					{
						++references;
					}
				}

				if (set->ShaderTableReferences > NumericLimits<uint32>::max() - references)
				{
					return Result<void>::Failure(EErrorCode::Conflict, "Rhi.ShaderTable", "The local set reference count is exhausted.");
				}
				localSets[localCount++] = setHandle;

				return Result<void>::Success();
			};
		Result<void> local = checkLocal(pipeline->GeneralLocalLayouts[desc.RayGenerationShaderIndex], desc.RayGenerationLocalSet);

		if (!local)
		{
			return Result<ShaderTableHandle>::Failure(local.GetError());
		}

		for (usize index = 0; index < desc.MissShaderIndices.Size(); ++index)
		{
			const BindingSetHandle set = desc.MissLocalSets.IsEmpty() ? BindingSetHandle{} : desc.MissLocalSets[index];
			local = checkLocal(pipeline->GeneralLocalLayouts[desc.MissShaderIndices[index]], set);

			if (!local)
			{
				return Result<ShaderTableHandle>::Failure(local.GetError());
			}
		}

		for (usize index = 0; index < desc.HitGroupIndices.Size(); ++index)
		{
			const BindingSetHandle set = desc.HitGroupLocalSets.IsEmpty() ? BindingSetHandle{} : desc.HitGroupLocalSets[index];
			local = checkLocal(pipeline->HitLocalLayouts[desc.HitGroupIndices[index]], set);

			if (!local)
			{
				return Result<ShaderTableHandle>::Failure(local.GetError());
			}
		}

		for (usize index = 0; index < desc.CallableShaderIndices.Size(); ++index)
		{
			const BindingSetHandle set = desc.CallableLocalSets.IsEmpty() ? BindingSetHandle{} : desc.CallableLocalSets[index];
			local = checkLocal(pipeline->GeneralLocalLayouts[desc.CallableShaderIndices[index]], set);

			if (!local)
			{
				return Result<ShaderTableHandle>::Failure(local.GetError());
			}
		}

		const bool bNewSlot = m_State->m_FreeShaderTable == 0xFFFFFFFFu;
		const uint32 slotIndex = bNewSlot ? static_cast<uint32>(m_State->m_ShaderTables.Size()) : m_State->m_FreeShaderTable;

		if (bNewSlot && (m_State->m_ShaderTables.Size() >= 0xFFFFFFFFu || !m_State->m_ShaderTables.Add(ShaderTableSlot(m_State->m_Resource))))
		{
			return Result<ShaderTableHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a shader table slot.");
		}
		ShaderTableSlot& slot = m_State->m_ShaderTables[slotIndex];

		if (!bNewSlot)
		{
			m_State->m_FreeShaderTable = slot.NextFree;
		}
		ASSERT(!slot.PreparedTable, "A recycled shader table has released its immutable native realization.");
		slot.PreparedReferences = 0;
		slot.LocalData = Move(copiedData);
		slot.TypedLocalSets = Move(typedSets);
		CopyBytes(slot.TypedLocalSetOffsets, typedSetOffsets, sizeof(typedSetOffsets));
		CopyBytes(slot.LocalDataOffsets, dataOffsets, sizeof(dataOffsets));
		slot.Pipeline = desc.Pipeline;
		slot.RayGenerationShaderIndex = desc.RayGenerationShaderIndex;
		slot.MissShaderCount = static_cast<uint32>(desc.MissShaderIndices.Size());
		slot.HitGroupCount = static_cast<uint32>(desc.HitGroupIndices.Size());
		slot.CallableShaderCount = static_cast<uint32>(desc.CallableShaderIndices.Size());
		slot.RayGenerationLocalSet = desc.RayGenerationLocalSet;

		for (uint32 index = 0; index < slot.MissShaderCount; ++index)
		{
			slot.MissShaderIndices[index] = desc.MissShaderIndices[index];
			slot.MissLocalSets[index] = desc.MissLocalSets.IsEmpty() ? BindingSetHandle{} : desc.MissLocalSets[index];
		}

		for (uint32 index = 0; index < slot.HitGroupCount; ++index)
		{
			slot.HitGroupIndices[index] = desc.HitGroupIndices[index];
			slot.HitGroupLocalSets[index] = desc.HitGroupLocalSets.IsEmpty() ? BindingSetHandle{} : desc.HitGroupLocalSets[index];
		}

		for (uint32 index = 0; index < slot.CallableShaderCount; ++index)
		{
			slot.CallableShaderIndices[index] = desc.CallableShaderIndices[index];
			slot.CallableLocalSets[index] = desc.CallableLocalSets.IsEmpty() ? BindingSetHandle{} : desc.CallableLocalSets[index];
		}

		for (uint32 index = 0; index < localCount; ++index)
		{
			++m_State->m_BindingSets[localSets[index].Slot].ShaderTableReferences;
		}
		for (rhi::BindingSetHandle const handle : slot.TypedLocalSets)
		{
			++m_State->m_BindingSets[handle.Slot].ShaderTableReferences;
		}
		slot.NextFree = 0xFFFFFFFFu;
		slot.bLive = true;
		++pipeline->ShaderTableReferences;

		return Result<ShaderTableHandle>::Success({ m_State->m_Owner, slotIndex, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyShaderTable(ShaderTableHandle handle)
	{
		CallScope call(*this, false);
		ShaderTableSlot* const slot = findShaderTable(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->PreparedReferences != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.ShaderTable", "A prepared command pins the shader table.");
		}
		RayTracingPipelineSlot* const pipeline = findRayTracingPipeline(slot->Pipeline);
		ASSERT(pipeline != nullptr && pipeline->ShaderTableReferences != 0, "A shader table must retain its ray pipeline.");
		--pipeline->ShaderTableReferences;
		auto releaseLocal = [&](BindingSetHandle set) noexcept
			{
				if (set.IsValid())
				{
					--m_State->m_BindingSets[set.Slot].ShaderTableReferences;
				}
			};
		releaseLocal(slot->RayGenerationLocalSet);

		for (uint32 index = 0; index < slot->MissShaderCount; ++index)
		{
			releaseLocal(slot->MissLocalSets[index]);
			slot->MissLocalSets[index] = {};
		}

		for (uint32 index = 0; index < slot->HitGroupCount; ++index)
		{
			releaseLocal(slot->HitGroupLocalSets[index]);
			slot->HitGroupLocalSets[index] = {};
		}

		for (uint32 index = 0; index < slot->CallableShaderCount; ++index)
		{
			releaseLocal(slot->CallableLocalSets[index]);
			slot->CallableLocalSets[index] = {};
		}
		slot->RayGenerationLocalSet = {};
		for (rhi::BindingSetHandle const set : slot->TypedLocalSets)
		{
			releaseLocal(set);
		}
		slot->TypedLocalSets.Reset();
		slot->PreparedTable.Reset();
		slot->LocalData.Reset();
		slot->Pipeline = {};
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeShaderTable;
			m_State->m_FreeShaderTable = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::DispatchRays(CommandListHandle list, const RayBindings& dispatchBindings, const RayDispatch& dispatch)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");
		const auto fail = [&](const Error& error)
			{
				ASSERT(error.Code != EErrorCode::InvalidArgument, "Ray dispatch bindings, arguments, and resource aliases must be valid.");
				poisonCommandList(list);

				return Result<void>::Failure(error);
			};
		if (!dispatchBindings.Resources.DeviceHeaps.IsEmpty())
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.RayDispatch", "Ray device-heap binding is unavailable.");
		}
		ShaderTableSlot* const table = findShaderTable(dispatchBindings.Table);

		ASSERT(!(command == nullptr || table == nullptr), "RHI operations require valid handles for this device.");
		RayTracingPipelineSlot* const pipeline = findRayTracingPipeline(table->Pipeline);
		ASSERT(pipeline != nullptr, "A shader table must retain its ray pipeline.");
		if (pipeline->bTypedLocalRoots)
		{
			return dispatchTypedRays(list, *command, *table, *pipeline, dispatchBindings, dispatch);
		}
		if (pipeline->LayoutCount != 0)
		{
			bool bHasSingleLocalSet = table->RayGenerationLocalSet.IsValid();
			for (
				uint32 index = 0; index < table->MissShaderCount; ++index)
			{
				bHasSingleLocalSet |= table->MissLocalSets[index].IsValid();
			}
			for (uint32 index = 0; index < table->HitGroupCount; ++index)
			{
				bHasSingleLocalSet |= table->HitGroupLocalSets[index].IsValid();
			}
			for (uint32 index = 0; index < table->CallableShaderCount; ++index)
			{
				bHasSingleLocalSet |= table->CallableLocalSets[index].IsValid();
			}
			if (!bHasSingleLocalSet)
			{
				return dispatchTypedRays(list, *command, *table, *pipeline, dispatchBindings, dispatch);
			}
		}

		Result<AccelStructSlot*> resolvedAccel = resolveAccelStruct(pipeline->AccelStructSlot, AccelStructHandle{});

		if (!resolvedAccel)
		{
			return fail(resolvedAccel.GetError());
		}
		AccelStructSlot* const accel = resolvedAccel.Value();

		ASSERT(!((command->Queue != EQueue::Graphics && command->Queue != EQueue::Compute) ||
				command->bRendering ||
				dispatch.Width == 0 ||
				dispatch.Height == 0 ||
				dispatch.Depth == 0 ||
				dispatch.PushConstants.Size() != pipeline->PushConstantBytes), "Ray dispatch bindings, arguments, and resource aliases must be valid.");

		if constexpr (backend::HAS_RECORD_RAY_DISPATCH == false)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.RayDispatch", "Ray dispatch is unavailable.");
		}

		ASSERT(!(dispatchBindings.Resources.Sets.Size() != pipeline->LayoutCount), "Ray dispatch bindings, arguments, and resource aliases must be valid.");
		BindingSetHandle setHandles[4]{};
		BindingSetSlot* sets[4]{};
		Span<const TextureHandle> sampled[4]{};
		Span<const TextureHandle> storage[4]{};
		NativeComputeBuffer buffers[4][8]{};
		NativeComputeTexture nativeTextures[4][8]{};
		NativeBindingSet nativeSets[4]{};
		uint32 totalBuffers = 0;
		uint32 totalTextures = 0;
		uint32 totalSamplers = 0;
		uint32 totalViews = 0;
		uint32 totalAccels = 0;

		for (uint32 layoutIndex = 0; layoutIndex < pipeline->LayoutCount; ++layoutIndex)
		{
			const RayTracingPipelineSlot::GlobalLayout& layout = pipeline->Layouts[layoutIndex];
			const BindingSetHandle setHandle = dispatchBindings.Resources.Sets[layoutIndex];
			BindingSetSlot* const set = findBindingSet(setHandle);

			ASSERT(set != nullptr, "RHI operations require valid handles for this device.");
			for (uint32 accelIndex = 0; accelIndex < set->AccelStructCount; ++accelIndex)
			{
				const AccelStructSlot* boundAccel = findAccelStruct(set->AccelStructs[accelIndex]);
				ASSERT(boundAccel != nullptr, "A binding set retains its acceleration structures.");
				ASSERT(!(!boundAccel->bBuilt || boundAccel->bUnavailable || boundAccel->Type != EAccelStructType::TopLevel), "Ray dispatch bindings, arguments, and resource aliases must be valid.");
			}
			totalAccels += set->AccelStructCount;

			ASSERT(!(set->Layout != layout.Handle || set->BufferCount != layout.GlobalBufferCount), "Ray dispatch bindings, arguments, and resource aliases must be valid.");
			setHandles[layoutIndex] = setHandle;
			sets[layoutIndex] = set;
			const Span<const TextureHandle> resolvedSampled = resolveComputeTextures(layout.Handle, setHandle, {});

			const Span<const TextureHandle> resolvedStorage = resolveComputeStorageTextures(layout.Handle, setHandle, {});
			sampled[layoutIndex] = resolvedSampled;
			storage[layoutIndex] = resolvedStorage;
			Result<void> validTextures = validateComputeTextures(layout, sampled[layoutIndex], storage[layoutIndex], *command, list.Slot, setHandle, nativeTextures[layoutIndex]);

			if (!validTextures)
			{
				return fail(validTextures.GetError());
			}

			for (uint32 index = 0; index < layout.TextureCount; ++index)
			{
				for (uint32 previousLayout = 0; previousLayout < layoutIndex; ++previousLayout)
				{
					for (uint32 previous = 0; previous < pipeline->Layouts[previousLayout].TextureCount; ++previous)
					{
						ASSERT(!(nativeTextures[layoutIndex][index].Texture == nativeTextures[previousLayout][previous].Texture), "Ray dispatch bindings, arguments, and resource aliases must be valid.");
					}
				}
			}

			for (uint32 index = 0; index < layout.GlobalBufferCount; ++index)
			{
				BufferSlot* const buffer = findBuffer(set->Buffers[index]);

				ASSERT(buffer != nullptr, "RHI operations require valid handles for this device.");

				const ComputeBindingDesc binding = layout.GlobalBuffers[index];
				const BufferRange range = resolveBufferRange(setHandle, index, buffer->ByteCount);
				const EBufferUsage usage = binding.Type == EComputeBindingType::UniformBuffer ? EBufferUsage::Uniform : EBufferUsage::Storage;

				ASSERT(!(!HasUsage(buffer->Usage, usage) || (binding.Type == EComputeBindingType::UniformBuffer && range.ByteCount > 65536)), "Ray dispatch bindings, arguments, and resource aliases must be valid.");

				for (uint32 previousLayout = 0; previousLayout <= layoutIndex; ++previousLayout)
				{
					for (uint32 previous = 0;
						previous < (previousLayout == layoutIndex ? index : pipeline->Layouts[previousLayout].GlobalBufferCount);
						++previous)
					{
						if (sets[previousLayout]->Buffers[previous] != set->Buffers[index])
						{
							continue;
						}

						const ComputeBindingDesc& oldBinding = pipeline->Layouts[previousLayout].GlobalBuffers[previous];

						ASSERT(!(binding.Type != oldBinding.Type ||
								!canShareBuffer(binding.Type != EComputeBindingType::StorageBuffer, oldBinding.Type != EComputeBindingType::StorageBuffer, range, resolveBufferRange(setHandles[previousLayout], previous, buffer->ByteCount))), "Ray dispatch bindings, arguments, and resource aliases must be valid.");
					}
				}

				Result<void*> bufferView = resolveBoundBufferView(setHandles[layoutIndex], index, set->Buffers[index], binding.Format, binding.Type == EComputeBindingType::ReadOnlyBuffer);

				if (!bufferView)
				{
					return fail(bufferView.GetError());
				}
				buffers[layoutIndex][index] = { buffer->Native,	binding.Type,		binding.Slot,  range.ByteOffset,
											   range.ByteCount, bufferView.Value(), binding.Format };
			}
			nativeSets[layoutIndex] = { {buffers[layoutIndex], layout.GlobalBufferCount},
									   {nativeTextures[layoutIndex], layout.TextureCount} };
			totalBuffers += layout.GlobalBufferCount;
			totalTextures += layout.TextureCount;
			totalSamplers += set->SamplerCount;
			totalViews += set->TextureCount + set->StorageTextureCount;
		}
		BindingSetHandle localHandles[97]{};
		NativeComputeBuffer localBuffers[97][8]{};
		NativeComputeTexture localTextures[97][8]{};
		Span<const TextureHandle> localSampled[97]{};
		Span<const TextureHandle> localStorage[97]{};
		NativeBindingSet nativeLocals[97]{};
		const uint32 localRecordCount = 1 + table->MissShaderCount + table->HitGroupCount + table->CallableShaderCount;
		localHandles[0] = table->RayGenerationLocalSet;

		for (uint32 index = 0; index < table->MissShaderCount; ++index)
		{
			localHandles[1 + index] = table->MissLocalSets[index];
		}

		for (uint32 index = 0; index < table->HitGroupCount; ++index)
		{
			localHandles[1 + table->MissShaderCount + index] = table->HitGroupLocalSets[index];
		}

		for (uint32 index = 0; index < table->CallableShaderCount; ++index)
		{
			localHandles[1 + table->MissShaderCount + table->HitGroupCount + index] = table->CallableLocalSets[index];
		}

		for (uint32 record = 0; record < localRecordCount; ++record)
		{
			const uint32 localBytes = table->LocalDataOffsets[record + 1] - table->LocalDataOffsets[record];
			const Span<const Byte> localData =
				localBytes != 0 ? Span<const Byte>{table->LocalData.Data() + table->LocalDataOffsets[record], localBytes}
			: Span<const Byte>{};
			nativeLocals[record].LocalData = localData;
			const BindingSetHandle setHandle = localHandles[record];

			if (!setHandle.IsValid())
			{
				continue;
			}
			BindingSetSlot* const set = findBindingSet(setHandle);
			ASSERT(set != nullptr, "A shader table must retain its local set.");
			const BindingLayoutSlot& layout = m_State->m_BindingLayouts[set->Layout.Slot];
			RayTracingPipelineSlot::RayTracingPipelineSlot::GlobalLayout textureLayout{};
			textureLayout.TextureCount = layout.ComputeTextureCount;
			textureLayout.SampledTextureCount = layout.ComputeSampledCount;
			textureLayout.StorageTextureCount = layout.ComputeStorageCount;

			for (uint32 index = 0; index < layout.ComputeTextureCount; ++index)
			{
				textureLayout.Textures[index] = layout.ComputeTexture(index);
			}

			const Span<const TextureHandle> resolvedSampled = resolveComputeTextures(set->Layout, setHandle, {});

			const Span<const TextureHandle> resolvedStorage = resolveComputeStorageTextures(set->Layout, setHandle, {});
			localSampled[record] = resolvedSampled;
			localStorage[record] = resolvedStorage;
			Result<void> validTextures = validateComputeTextures(textureLayout, localSampled[record], localStorage[record], *command, list.Slot, setHandle, localTextures[record]);

			if (!validTextures)
			{
				return fail(validTextures.GetError());
			}

			for (uint32 index = 0; index < layout.ComputeTextureCount; ++index)
			{
				const NativeComputeTexture& current = localTextures[record][index];

				for (uint32 global = 0; global < pipeline->LayoutCount; ++global)
				{
					for (uint32 previous = 0; previous < pipeline->Layouts[global].TextureCount; ++previous)
					{
						ASSERT(!(current.Texture == nativeTextures[global][previous].Texture &&
								(current.Type == EComputeTextureBindingType::StorageTexture || nativeTextures[global][previous].Type == EComputeTextureBindingType::StorageTexture)), "Ray dispatch bindings, arguments, and resource aliases must be valid.");
					}
				}

				for (uint32 previousRecord = 0; previousRecord <= record; ++previousRecord)
				{
					for (uint32 previous = 0;
						previous < (previousRecord == record ? index : nativeLocals[previousRecord].Textures.Size()); ++previous)
					{
						const rhi::NativeComputeTexture& old = previousRecord == record ? localTextures[record][previous] : nativeLocals[previousRecord].Textures[previous];

						ASSERT(!(current.Texture == old.Texture && current.Type != old.Type), "Ray dispatch bindings, arguments, and resource aliases must be valid.");
					}
				}
			}

			for (uint32 index = 0; index < layout.ComputeCount; ++index)
			{
				BufferSlot* const buffer = findBuffer(set->Buffers[index]);

				ASSERT(buffer != nullptr, "RHI operations require valid handles for this device.");

				const ComputeBindingDesc binding = layout.ComputeBuffer(index);
				const BufferRange range = resolveBufferRange(setHandle, index, buffer->ByteCount);
				const EBufferUsage usage = binding.Type == EComputeBindingType::UniformBuffer ? EBufferUsage::Uniform : EBufferUsage::Storage;

				ASSERT(!(!HasUsage(buffer->Usage, usage) || (binding.Type == EComputeBindingType::UniformBuffer && range.ByteCount > 65536)), "Ray dispatch bindings, arguments, and resource aliases must be valid.");

				ASSERT(!(m_State->m_Kind == EBackend::Vulkan && binding.Format == EFormat::UNKNOWN && !HasUsage(buffer->Usage, EBufferUsage::DeviceAddress)), "Ray dispatch bindings, arguments, and resource aliases must be valid.");

				for (uint32 global = 0; global < pipeline->LayoutCount; ++global)
				{
					for (uint32 previous = 0; previous < pipeline->Layouts[global].GlobalBufferCount; ++previous)
					{
						if (sets[global]->Buffers[previous] != set->Buffers[index])
						{
							continue;
						}

						const ComputeBindingDesc oldBinding = pipeline->Layouts[global].GlobalBuffers[previous];

						ASSERT(!(binding.Type != oldBinding.Type ||
								!canShareBuffer(binding.Type != EComputeBindingType::StorageBuffer, oldBinding.Type != EComputeBindingType::StorageBuffer, range, resolveBufferRange(setHandles[global], previous, buffer->ByteCount))), "Ray dispatch bindings, arguments, and resource aliases must be valid.");
					}
				}

				for (uint32 previousRecord = 0; previousRecord <= record; ++previousRecord)
				{
					if (!localHandles[previousRecord].IsValid())
					{
						continue;
					}

					const BindingSetSlot* const oldSet = findBindingSet(localHandles[previousRecord]);
					const BindingLayoutSlot& oldLayout = m_State->m_BindingLayouts[oldSet->Layout.Slot];

					for (uint32 previous = 0;
						previous < (previousRecord == record ? index : oldLayout.ComputeCount); ++previous)
					{
						if (oldSet->Buffers[previous] != set->Buffers[index])
						{
							continue;
						}

						const ComputeBindingDesc oldBinding = oldLayout.ComputeBuffer(previous);

						ASSERT(!(binding.Type != oldBinding.Type ||
								!canShareBuffer( binding.Type != EComputeBindingType::StorageBuffer, oldBinding.Type != EComputeBindingType::StorageBuffer, range, resolveBufferRange(localHandles[previousRecord], previous, buffer->ByteCount))), "Ray dispatch bindings, arguments, and resource aliases must be valid.");
					}
				}

				Result<void*> bufferView = resolveBoundBufferView(localHandles[record], index, set->Buffers[index], binding.Format, binding.Type == EComputeBindingType::ReadOnlyBuffer);

				if (!bufferView)
				{
					return fail(bufferView.GetError());
				}
				localBuffers[record][index] = { buffer->Native,	binding.Type,		binding.Slot,  range.ByteOffset,
											   range.ByteCount, bufferView.Value(), binding.Format };
			}
			nativeLocals[record] = { {localBuffers[record], layout.ComputeCount},
									{localTextures[record], layout.ComputeTextureCount},
									localData };
			totalBuffers += layout.ComputeCount;
			totalTextures += layout.ComputeTextureCount;
			totalSamplers += set->SamplerCount;
			totalViews += set->TextureCount + set->StorageTextureCount;
		}

		if (!command->RayPipelineUses.Reserve(command->RayPipelineUses.Size() + 1) ||
			!command->BufferUses.Reserve(command->BufferUses.Size() + totalBuffers + totalAccels + (accel != nullptr ? 1 : 0)) ||
			!command->TextureUses.Reserve(command->TextureUses.Size() + totalTextures) ||
			!command->SamplerUses.Reserve(command->SamplerUses.Size() + totalSamplers) ||
			!command->TextureViewUses.Reserve(command->TextureViewUses.Size() + totalViews) ||
			!reserveAccelStructUse(*command, accel) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + totalAccels + (accel != nullptr ? 1u : 0u)))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track ray dispatch resources.");
		}

		const NativeShaderTableDesc native{
			table->RayGenerationShaderIndex,
			{table->MissShaderIndices, table->MissShaderCount},
			{table->HitGroupIndices, table->HitGroupCount},
			{table->CallableShaderIndices, table->CallableShaderCount},
			pipeline->GeneralShaderCount,
			nativeLocals[0],
			{nativeLocals + 1, table->MissShaderCount},
			{nativeLocals + 1 + table->MissShaderCount, table->HitGroupCount},
			{nativeLocals + 1 + table->MissShaderCount + table->HitGroupCount, table->CallableShaderCount
					}
		};
		PreparedBinding globalBinding;
		globalBinding.SetCount = pipeline->LayoutCount;

		for (uint32 index = 0; index < pipeline->LayoutCount; ++index)
		{
			globalBinding.Sets[index] = setHandles[index];
		}
		NativeFixedDescriptor fixedDescriptors[MAXIMUM_FIXED_BINDING_LAYOUTS][32]{};
		NativeFixedBindingSet fixedSets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		fillPreparedNativeSets(globalBinding, fixedDescriptors, fixedSets);
		bool bHasTables = false;

		for (uint32 index = 0; index < pipeline->LayoutCount; ++index)
		{
			bHasTables |= static_cast<bool>(sets[index]->Tables);
		}
		if (bHasTables)
		{
			BindingSetHandle accessSets[101]{}; uint32 accessSetCount = 0;
			for (uint32 index = 0; index < pipeline->LayoutCount; ++index)
			{
				accessSets[accessSetCount++] = setHandles[index];
			}
			for (

				uint32 index = 0; index < localRecordCount; ++index)
			{
				if (localHandles[index].IsValid())
				{
					accessSets[accessSetCount++] = localHandles[index];
				}
			}

			Result<void> tableUses = prepareOrdinaryBoundTableUses(*command, { accessSets, accessSetCount });

			if (!tableUses)
			{
				return fail(tableUses.GetError());
			}
		}
		usize tableUseCount = 0;
		usize tableRangeCount = 0;
		for (uint32 index = 0; index < pipeline->LayoutCount; ++index)
		{
			if (!sets[index]->Tables)
			{
				continue;
			}
			const BoundDescriptorTables& owner = *sets[index]->Tables;
			tableRangeCount += owner.Ranges.Size();
			for (const BoundDescriptorTableRange& row : owner.Ranges)
			{
				tableUseCount += row.UsedCount;
				const DescriptorTableSlot& descriptorTable = m_State->m_DescriptorTables[row.Table.Slot];
				for (uint32 used = 0; used < row.UsedCount; ++used)
				{
					const DescriptorTableEntry& entry = descriptorTable.Entries[owner.UsedEntries[row.UsedOffset + used]];

					if (entry.BottomReferences)
					{
						tableUseCount += entry.BottomReferences->Size();
					}
				}
			}
		}
		if (!command->BufferUses.Reserve(command->BufferUses.Size() + totalBuffers + totalAccels + tableUseCount + (accel != nullptr ? 1u : 0u)) ||
			!command->TextureUses.Reserve(command->TextureUses.Size() + totalTextures + tableUseCount) ||
			!command->SamplerUses.Reserve(command->SamplerUses.Size() + totalSamplers + tableUseCount) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + totalAccels + tableUseCount + ( accel != nullptr ? 1u : 0u)))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain ray binding resources.");
		}

		if (!command->DescriptorTableUses.Reserve
		(command->DescriptorTableUses.Size() + tableRangeCount))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain global ray table uses.");
		}
		(void)command->RayPipelineUses.Add({ table->Pipeline, pipeline->Native });
		++pipeline->RecordedUses;

		for (uint32 layoutIndex = 0; layoutIndex < pipeline->LayoutCount; ++layoutIndex)
		{
			const RayTracingPipelineSlot::GlobalLayout& layout = pipeline->Layouts[layoutIndex];

			for (uint32 index = 0; index < layout.GlobalBufferCount; ++index)
			{
				const BufferHandle handle = sets[layoutIndex]->Buffers[index];
				(void)command->AddBufferUse({ handle, m_State->m_Buffers[handle.Slot].Native, BufferUseState(layout.GlobalBuffers[index].Type) });
				++m_State->m_Buffers[handle.Slot].RecordedUses;
			}
			trackGraphicsTextures(*command, list.Slot, sampled[layoutIndex]);
			trackGraphicsTextures(*command, list.Slot, storage[layoutIndex], ETrackedResourceState::Storage);
			trackBoundSamplers(*command, setHandles[layoutIndex]);
			trackBoundViews(*command, setHandles[layoutIndex]);
			for (uint32 accelIndex = 0; accelIndex < sets[layoutIndex]->AccelStructCount; ++accelIndex)
			{
				const AccelStructHandle handle = sets[layoutIndex]->AccelStructs[accelIndex];
				trackAccelStruct(*command, handle, findAccelStruct(handle));
			}
			if (sets[layoutIndex]->Tables)
			{
				for (const BoundDescriptorTableRange& row : sets[layoutIndex]->Tables->Ranges)
				{
					trackBoundTableUses(*command, row, *sets[layoutIndex]->Tables);
				}
			}
		}

		for (uint32 record = 0; record < localRecordCount; ++record)
		{
			if (!localHandles[record].IsValid())
			{
				continue;
			}
			BindingSetSlot* const set = findBindingSet(localHandles[record]);
			const BindingLayoutSlot& layout = m_State->m_BindingLayouts[set->Layout.Slot];

			for (uint32 index = 0; index < layout.ComputeCount; ++index)
			{
				const BufferHandle handle = set->Buffers[index];
				(void)command->AddBufferUse({ handle, m_State->m_Buffers[handle.Slot].Native, BufferUseState(layout.ComputeBuffer(index).Type) });
				++m_State->m_Buffers[handle.Slot].RecordedUses;
			}
			trackGraphicsTextures(*command, list.Slot, localSampled[record]);
			trackGraphicsTextures(*command, list.Slot, localStorage[record], ETrackedResourceState::Storage);
			trackBoundSamplers(*command, localHandles[record]);
			trackBoundViews(*command, localHandles[record]);
		}
		trackAccelStruct(*command, AccelStructHandle{}, accel);
		Result<void> recorded = backend::RecordRayDispatch(m_State->m_Backend.State, command->Native, pipeline->Native, native,
			{ nativeSets, pipeline->LayoutCount }, { fixedSets, pipeline->LayoutCount }, dispatch.PushConstants, accel != nullptr ? accel->Native : nullptr, dispatch.Width,
			dispatch.Height, dispatch.Depth);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}
}
