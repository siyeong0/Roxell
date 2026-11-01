#pragma once
#include "VulkanBackend.h"
namespace rox::vulkan
{
	[[nodiscard]] VkDescriptorType RangeDescriptorType(const BindingRangeDesc& range) noexcept;

	[[nodiscard]] constexpr core::uint32 SampledDescriptorCapacity(const VkPhysicalDeviceLimits& limits) noexcept
	{
		if (limits.maxBoundDescriptorSets < 2)
		{
			return 0;
		}

		return core::Min(core::Min(limits.maxPerStageDescriptorSampledImages, limits.maxDescriptorSetSampledImages),
			limits.maxPerStageResources);
	}

	template <typename TLayout>
	[[nodiscard]] core::Result<void> ValidateRangeDescriptorLimits(const State& state, core::Span<const TLayout> layouts, bool bGraphics, bool bRay = false)
	{
		// Uniform texel descriptors share sampled-image limits; storage texel descriptors share storage-image limits.
		core::uint64 totals[6]{};
		core::uint64 stages[14][6]{};
		for (const TLayout& layout : layouts)
		{
			for (core::uint32 rangeIndex = 0; rangeIndex < layout.Ranges.Size(); ++rangeIndex)
			{
				const BindingRangeDesc range = layout.Range(rangeIndex);
				const VkDescriptorType type = RangeDescriptorType(range);
				const core::uint32 family = type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR ? 5u
					: type == VK_DESCRIPTOR_TYPE_SAMPLER ? 0u
					: type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? 1u
					: type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ? 2u
					: type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
					? 3u
					: 4u;
				totals[family] += range.DescriptorCount;
				const core::uint32 visibility = static_cast<core::uint32>(range.Visibility);
				for (core::uint32 stage = 0; stage < 14; ++stage)
				{
					if ((bRay ? stage >= 5 && stage < 11 && (visibility & (1u << stage)) != 0 : bGraphics ? ((stage > 0 && stage < 5) || stage >= 11) && (visibility & (1u << stage)) != 0 : stage == 0))
					{
						stages[stage][family] += range.DescriptorCount;
					}
				}
			}
		}
		const VkPhysicalDeviceLimits& limits = state.DeviceProperties.limits;
		const core::uint32 totalLimits[]{
			limits.maxDescriptorSetSamplers,	   limits.maxDescriptorSetUniformBuffers,
			limits.maxDescriptorSetStorageBuffers, limits.maxDescriptorSetSampledImages,
			limits.maxDescriptorSetStorageImages,  state.AccelerationProperties.maxDescriptorSetAccelerationStructures };
		const core::uint32 stageLimits[]{
			limits.maxPerStageDescriptorSamplers,		limits.maxPerStageDescriptorUniformBuffers,
			limits.maxPerStageDescriptorStorageBuffers, limits.maxPerStageDescriptorSampledImages,
			limits.maxPerStageDescriptorStorageImages,	state.AccelerationProperties.maxPerStageDescriptorAccelerationStructures };
		for (core::uint32 family = 0; family < 6; ++family)
		{
			if (totals[family] > totalLimits[family])
			{
				return core::Result<void>::Failure(core::EErrorCode::Unsupported, "Rhi.BindingRange", "Pipeline descriptor totals exceed native limits.");
			}
		}
		for (const core::uint64(&counts)[6] : stages)
		{
			core::uint64 resources = 0;
			for (core::uint32 family = 0; family < 6; ++family)
			{
				if (counts[family] > stageLimits[family])
				{
					return core::Result<void>::Failure(core::EErrorCode::Unsupported, "Rhi.BindingRange", "Per-stage descriptors exceed native limits.");
				}
				if (family != 0 && family != 5)
				{
					resources += counts[family];
				}
			}
			if (resources > limits.maxPerStageResources)
			{
				return core::Result<void>::Failure(core::EErrorCode::Unsupported, "Rhi.BindingRange", "Per-stage resource totals exceed native limits.");
			}
		}

		return core::Result<void>::Success();
	}

	template <typename TPipeline, typename TLayout>
	[[nodiscard]] core::Result<void> CreateRangeLayouts(State& state, TPipeline& pipeline, core::Span<const TLayout> layouts, bool bGraphics, bool bRay = false)
	{
		core::Result<void> limits = ValidateRangeDescriptorLimits(state, layouts, bGraphics, bRay);
		if (!limits)
		{
			return limits;
		}
		pipeline.Fixed.LayoutCount = static_cast<core::uint32>(layouts.Size());
		core::uint32 setCount = 0;
		for (core::uint32 index = 0; index < layouts.Size(); ++index)
		{
			const TLayout& source = layouts[index];
			ASSERT(source.Space < MAXIMUM_FIXED_BINDING_LAYOUTS, "Range spaces have validated fixed capacity.");
			setCount = core::Max(setCount, source.Space + 1);
			pipeline.Fixed.Spaces[index] = source.Space;
			pipeline.Fixed.RangeCounts[index] = static_cast<core::uint32>(source.Ranges.Size());
			for (core::uint32 range = 0; range < source.Ranges.Size(); ++range)
			{
				pipeline.Fixed.Ranges[index][range] = source.Range(range);
				pipeline.Fixed.Ranges[index][range].ImmutableSamplerOrNull = nullptr;
			}
		}
		if (setCount > state.DeviceProperties.limits.maxBoundDescriptorSets)
		{
			return core::Result<void>::Failure(core::EErrorCode::Unsupported, "Rhi.BindingRange", "Native descriptor spaces exceed the device limit.");
		}
		pipeline.SetLayoutCount = setCount;
		for (core::uint32 space = 0; space < setCount; ++space)
		{
			VkDescriptorSetLayoutBinding bindings[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
			VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			for (const TLayout& source : layouts)
			{
				if (source.Space != space)
				{
					continue;
				}
				info.bindingCount = static_cast<core::uint32>(source.Ranges.Size());
				for (core::uint32 index = 0; index < source.Ranges.Size(); ++index)
				{
					const BindingRangeDesc range = source.Range(index);
					const core::uint32 visibility = static_cast<core::uint32>(range.Visibility);
					VkShaderStageFlags stages =
						bRay ? ((visibility & 32u ? VK_SHADER_STAGE_RAYGEN_BIT_KHR : 0) |
							(visibility & 64u ? VK_SHADER_STAGE_MISS_BIT_KHR : 0) |
							(visibility & 128u ? VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR : 0) |
							(visibility & 256u ? VK_SHADER_STAGE_ANY_HIT_BIT_KHR : 0) |
							(visibility & 512u ? VK_SHADER_STAGE_INTERSECTION_BIT_KHR : 0) |
							(visibility & 1024u ? VK_SHADER_STAGE_CALLABLE_BIT_KHR : 0))
						: bGraphics
						? ((visibility & 2u ? VK_SHADER_STAGE_VERTEX_BIT : 0) | (visibility & 4u ? VK_SHADER_STAGE_FRAGMENT_BIT : 0) |
							(visibility & 8u ? VK_SHADER_STAGE_MESH_BIT_EXT : 0) | (visibility & 16u ? VK_SHADER_STAGE_TASK_BIT_EXT : 0) |
							(visibility & 2048u ? VK_SHADER_STAGE_GEOMETRY_BIT : 0) |
							(visibility & 4096u ? VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT : 0) |
							(visibility & 8192u ? VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT : 0))
						: VK_SHADER_STAGE_COMPUTE_BIT;
					bindings[index] = { range.Binding, RangeDescriptorType(range), range.DescriptorCount, stages, nullptr };
				}
			}
			info.pBindings = bindings;
			if (state.F.CreateDescriptorSetLayout(state.Device, &info, nullptr, &pipeline.SetLayouts[space]) != VK_SUCCESS)
			{
				return core::Result<void>::Failure(Failure("Rhi.BindingRange", "Could not create a native descriptor range layout."));
			}
		}

		return core::Result<void>::Success();
	}

	[[nodiscard]] core::Result<void> CreateComputeFixedLayout(State& state, ComputePipeline& pipeline, core::uint32 layoutIndex, const NativeBindingLayoutDesc& layout, core::uint32 accelSlot);
}
