#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<void*> CreateTextureView(State* opaque, void* nativeTexture, const TextureDesc& source, const TextureViewDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		if (desc.Aspect == ETextureAspect::Stencil && desc.BaseMipLevel != 0 && !SupportsNonzeroMipStencilViews(state))
		{
			return Result<void*>::Failure(
				EErrorCode::Unsupported,
				"Rhi.VulkanStencilMipViews",
				"NVIDIA RTX 5070 driver 617.14 cannot address nonzero-base-mip stencil sampled views correctly.");
		}
		const uint32 mipDepth = (source.Depth >> desc.BaseMipLevel) ? (source.Depth >> desc.BaseMipLevel) : 1;
		const bool bSliced = desc.BaseDepthSlice != 0 || (desc.DepthSliceCount != 0 && desc.DepthSliceCount != mipDepth);

		if (bSliced && !state.bImageSlicedViewOf3D)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureView", "Sliced 3D storage views require the enabled VK_EXT_image_sliced_view_of_3d feature.");
		}
		TextureView* const view = CreateObject<TextureView>(state.Resource);

		if (view == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a texture view."));
		}
		view->Descriptor = desc;
		VkImageViewCreateInfo native{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		VkImageViewUsageCreateInfo usage{ VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO };
		usage.usage = desc.Type == ETextureViewType::Storage ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT;
		native.pNext = &usage;
		VkImageViewSlicedCreateInfoEXT slicedInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_SLICED_CREATE_INFO_EXT };
		if (bSliced)
		{
			slicedInfo.sliceOffset = desc.BaseDepthSlice;
			slicedInfo.sliceCount = desc.DepthSliceCount != 0 ? desc.DepthSliceCount : VK_REMAINING_3D_SLICES_EXT;
			usage.pNext = &slicedInfo;
		}
		native.image = static_cast<Texture*>(nativeTexture)->Handle;

		switch (desc.Dimension)
		{
		case ETextureViewDimension::Texture2D:
			native.viewType = VK_IMAGE_VIEW_TYPE_2D;
			break;
		case ETextureViewDimension::Texture2DArray:
			native.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			break;
		case ETextureViewDimension::Cube:
			native.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			break;
		case ETextureViewDimension::CubeArray:
			native.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			break;
		case ETextureViewDimension::Texture3D:
			native.viewType = VK_IMAGE_VIEW_TYPE_3D;
			break;
		case ETextureViewDimension::Texture1D:
			native.viewType = VK_IMAGE_VIEW_TYPE_1D;
			break;
		case ETextureViewDimension::Texture1DArray:
			native.viewType = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			break;

		default:
		{
			break;
		}
		}
		native.format = ConvertVulkanFormat(desc.Format == EFormat::UNKNOWN ? source.Format : desc.Format);
		native.subresourceRange.aspectMask = desc.Aspect == ETextureAspect::Stencil ? VK_IMAGE_ASPECT_STENCIL_BIT
			: desc.Aspect == ETextureAspect::Depth ? VK_IMAGE_ASPECT_DEPTH_BIT
			: desc.Aspect == ETextureAspect::Color ? VK_IMAGE_ASPECT_COLOR_BIT
			: HasUsage(source.Usage, ETextureUsage::DepthStencil) ? VK_IMAGE_ASPECT_DEPTH_BIT
			: VK_IMAGE_ASPECT_COLOR_BIT;
		native.subresourceRange.baseMipLevel = desc.BaseMipLevel;
		native.subresourceRange.levelCount = desc.MipLevelCount;
		native.subresourceRange.baseArrayLayer = desc.BaseArrayLayer;
		native.subresourceRange.layerCount = desc.ArrayLayerCount;

		if (state.F.CreateImageView(state.Device, &native, nullptr, &view->Handle) != VK_SUCCESS)
		{
			DestroyObject(state.Resource, view);

			return Result<void*>::Failure(Failure("Rhi.VulkanTextureView", "Could not create a texture image view."));
		}

		return Result<void*>::Success(view);
	}

	void DestroyTextureView(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		TextureView* const view = static_cast<TextureView*>(native);
		state.F.DestroyImageView(state.Device, view->Handle, nullptr);
		DestroyObject(state.Resource, view);
	}

	void FreeTexture(State& state, Texture* texture) noexcept
	{
		if (texture == nullptr)
		{
			return;
		}

		if (texture->Framebuffer)
		{
			state.F.DestroyFramebuffer(state.Device, texture->Framebuffer, nullptr);
		}

		if (texture->MsaaRenderPass)
		{
			state.F.DestroyRenderPass(state.Device, texture->MsaaRenderPass, nullptr);
		}

		if (texture->MsaaDepthRenderPass)
		{
			state.F.DestroyRenderPass(state.Device, texture->MsaaDepthRenderPass, nullptr);
		}

		if (texture->MsaaDepthLoadRenderPass)
		{
			state.F.DestroyRenderPass(state.Device, texture->MsaaDepthLoadRenderPass, nullptr);
		}

		if (texture->View)
		{
			state.F.DestroyImageView(state.Device, texture->View, nullptr);
		}

		if (texture->StorageView)
		{
			state.F.DestroyImageView(state.Device, texture->StorageView, nullptr);
		}

		if (texture->Handle && !texture->bExternalImage)
		{
			state.F.DestroyImage(state.Device, texture->Handle, nullptr);
		}

		if (texture->Memory && texture->bOwnsMemory)
		{
			state.F.FreeMemory(state.Device, texture->Memory, nullptr);
		}

		DestroyObject(state.Resource, texture);
	}

	[[nodiscard]] Result<uint32> QueryTextureSampleCounts(const State* opaque, const TextureDesc& desc)
	{
		const State& state = *opaque;
		if ((desc.Dimension == ETextureDimension::Cube && desc.ArrayLayers > 6 && !state.bCubeArray) || (desc.bSparse && !state.bSparseResidency))
		{
			return Result<uint32>::Success(0);
		}
		const VkFormat format = ConvertVulkanFormat(desc.Format);
		if (format == VK_FORMAT_UNDEFINED)
		{
			return Result<uint32>::Success(0);
		}

		const VkImageCreateInfo info = MakeImageInfo(state, desc, format);
		VkImageFormatProperties properties{};
		const VkResult result = state.F.GetImageFormatProperties(state.Physical, format, info.imageType, info.tiling, info.usage, info.flags, &properties);
		if (result == VK_ERROR_FORMAT_NOT_SUPPORTED)
		{
			return Result<uint32>::Success(0);
		}
		if (result != VK_SUCCESS)
		{
			return Result<uint32>::Failure(Failure("Rhi.VulkanSamples", "Could not query image sample counts."));
		}
		if (desc.Width > properties.maxExtent.width ||
			desc.Height > properties.maxExtent.height ||
			desc.Depth > properties.maxExtent.depth ||
			desc.MipLevels > properties.maxMipLevels ||
			desc.ArrayLayers > properties.maxArrayLayers)
		{
			return Result<uint32>::Success(0);
		}

		return Result<uint32>::Success(static_cast<uint32>(properties.sampleCounts));
	}

	[[nodiscard]] bool SupportsColorResolve(const State* opaque, EFormat format) noexcept
	{
		const State& state = *opaque;
		VkFormatProperties properties{};
		state.F.GetFormatProperties(state.Physical, ConvertVulkanFormat(format), &properties);
		const int required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

		return (properties.optimalTilingFeatures & required) == required;
	}

	[[nodiscard]] Result<void*> CreateTextureInternal(State* opaque, const TextureDesc& desc, Heap* placedHeap, uint64 offset, VkImage imported, ENativeTextureContents contents)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		if (desc.bSparse && (!state.bSparseResidency || imported != VK_NULL_HANDLE || placedHeap != nullptr))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanSparseTexture", "This device or creation path does not support sparse image residency.");
		}

		if (desc.Dimension == ETextureDimension::Cube && desc.ArrayLayers > 6 && !state.bCubeArray)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.TextureCube", "This Vulkan device does not support cube arrays.");
		}

		const VkFormat format = ConvertVulkanFormat(desc.Format);

		if (format == VK_FORMAT_UNDEFINED)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanFormat", "No Vulkan texture format mapping exists."));
		}
		VkFormatProperties properties{};
		state.F.GetFormatProperties(state.Physical, format, &properties);
		const VkFormatFeatureFlags required =
			(HasUsage(desc.Usage, ETextureUsage::CopySource) ? VK_FORMAT_FEATURE_TRANSFER_SRC_BIT : 0) |
			((HasUsage(desc.Usage, ETextureUsage::CopyDestination) || HasUsage(desc.Usage, ETextureUsage::RenderTarget) ||
				HasUsage(desc.Usage, ETextureUsage::DepthStencil))
				? VK_FORMAT_FEATURE_TRANSFER_DST_BIT
				: 0) |
			(HasUsage(desc.Usage, ETextureUsage::RenderTarget) ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT : 0) |
			(HasUsage(desc.Usage, ETextureUsage::DepthStencil) ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : 0) |
			(HasUsage(desc.Usage, ETextureUsage::ShaderRead) ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT : 0) |
			(HasUsage(desc.Usage, ETextureUsage::Storage) ? VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT : 0) |
			(HasUsage(desc.Usage, ETextureUsage::ShadingRateSource) ? VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR : 0);

		if ((properties.optimalTilingFeatures & required) != required)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanFormat", "The Vulkan format lacks a required usage feature.");
		}
		Texture* texture = CreateObject<Texture>(state.Resource, state.Resource);

		if (texture == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan texture state."));
		}
		texture->MipLevels = desc.MipLevels;
		texture->ArrayLayers = desc.ArrayLayers;
		texture->Width = desc.Width;
		texture->Height = desc.Height;
		texture->SampleCount = desc.SampleCount;
		texture->Format = desc.Format;
		texture->Dimension = desc.Dimension;
		texture->bShaderRead = HasUsage(desc.Usage, ETextureUsage::ShaderRead);
		texture->bStorage = HasUsage(desc.Usage, ETextureUsage::Storage);
		texture->bDepth = HasUsage(desc.Usage, ETextureUsage::DepthStencil);
		VkImageCreateInfo info = MakeImageInfo(state, desc, format);
		VkImageFormatProperties imageProperties{};
		if (state.F.GetImageFormatProperties(state.Physical, format, info.imageType, VK_IMAGE_TILING_OPTIMAL, info.usage, info.flags, &imageProperties) != VK_SUCCESS ||
			(imageProperties.sampleCounts & info.samples) == 0)
		{
			FreeTexture(state, texture);

			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.Multisample", "The Vulkan image usage does not support this sample count.");
		}

		if (imported != VK_NULL_HANDLE)
		{
			texture->Handle = imported;
			texture->bExternalImage = true;
			texture->bOwnsMemory = false;
			texture->Layout = contents == ENativeTextureContents::Preserve ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
		}
		else
		{
			if (state.F.CreateImage(state.Device, &info, nullptr, &texture->Handle) != VK_SUCCESS)
			{
				FreeTexture(state, texture);

				return Result<void*>::Failure(Failure("Rhi.VulkanTexture", "Could not create a Vulkan texture."));
			}

			if (desc.bSparse)
			{
				state.F.GetImageMemoryRequirements(state.Device, texture->Handle, &texture->SparseMemoryRequirements);
				uint32 sparseCount = 0;
				state.F.GetImageSparseMemoryRequirements(state.Device, texture->Handle, &sparseCount, nullptr);

				if (sparseCount != 1)
				{
					FreeTexture(state, texture);

					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanSparseTexture", "The image needs multiple sparse aspect bindings.");
				}
				state.F.GetImageSparseMemoryRequirements(state.Device, texture->Handle, &sparseCount, &texture->SparseRequirements);
				const VkSparseImageMemoryRequirements& sparse = texture->SparseRequirements;
				const uint64 tileBytes = texture->SparseMemoryRequirements.alignment;

				if (sparseCount != 1 ||
					sparse.formatProperties.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
					tileBytes == 0 ||
					tileBytes > NumericLimits<uint32>::max() ||
					sparse.formatProperties.imageGranularity.width == 0 ||
					sparse.formatProperties.imageGranularity.height == 0 ||
					sparse.formatProperties.imageGranularity.depth == 0 ||
					sparse.imageMipTailFirstLod > desc.MipLevels ||
					(sparse.imageMipTailFirstLod < desc.MipLevels && sparse.imageMipTailSize == 0) ||
					(sparse.imageMipTailSize % tileBytes) != 0)
				{
					FreeTexture(state, texture);

					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanSparseTexture", "The sparse image tile layout is unsupported.");
				}
				texture->bSparse = true;
				texture->bOwnsMemory = false;
			}
			else
			{
				VkMemoryDedicatedRequirements dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
				VkMemoryRequirements2 queried{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
				queried.pNext = &dedicated;
				VkImageMemoryRequirementsInfo2 requirementsInfo{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
				requirementsInfo.image = texture->Handle;
				state.F.GetImageMemoryRequirements2(state.Device, &requirementsInfo, &queried);
				const VkMemoryRequirements& requirements = queried.memoryRequirements;

				if (placedHeap != nullptr && dedicated.requiresDedicatedAllocation)
				{
					FreeTexture(state, texture);

					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.DedicatedAllocation", "The image requires a dedicated allocation.");
				}

				uint32 selected = placedHeap == nullptr ? UINT32_MAX : placedHeap->MemoryTypeIndex;

				if (placedHeap == nullptr || placedHeap->Memory == VK_NULL_HANDLE)
				{
					selected = SelectMemoryType(state, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, true);
				}

				if (selected == UINT32_MAX || (requirements.memoryTypeBits & (1u << selected)) == 0)
				{
					FreeTexture(state, texture);

					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanHeap", "The heap memory type is incompatible with this image.");
				}
				VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
				allocation.allocationSize = placedHeap == nullptr ? requirements.size : placedHeap->Descriptor.CapacityBytes;
				allocation.memoryTypeIndex = selected;
				VkMemoryDedicatedAllocateInfo dedicatedAllocation{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
				if (placedHeap == nullptr && (dedicated.requiresDedicatedAllocation || dedicated.prefersDedicatedAllocation))
				{
					dedicatedAllocation.image = texture->Handle;
					allocation.pNext = &dedicatedAllocation;
				}
				VkDeviceMemory memory = placedHeap != nullptr ? placedHeap->Memory : VK_NULL_HANDLE;
				const bool bAllocate = memory == VK_NULL_HANDLE;

				if ((bAllocate && state.F.AllocateMemory(state.Device, &allocation, nullptr, &memory) != VK_SUCCESS) ||
					state.F.BindImageMemory(state.Device, texture->Handle, memory, offset) != VK_SUCCESS)
				{
					if (bAllocate && memory)
					{
						state.F.FreeMemory(state.Device, memory, nullptr);
					}
					FreeTexture(state, texture);

					return Result<void*>::Failure(Failure("Rhi.VulkanMemory", "Could not allocate or bind Vulkan image memory."));
				}
				texture->Memory = memory;
				texture->bOwnsMemory = placedHeap == nullptr;

				if (placedHeap != nullptr)
				{
					placedHeap->Memory = memory;
					placedHeap->MemoryTypeIndex = selected;
				}
			}
		}

		if (HasUsage(desc.Usage, ETextureUsage::RenderTarget) || texture->bShaderRead || texture->bStorage || texture->bDepth)
		{
			VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view.image = texture->Handle;
			view.viewType = desc.Dimension == ETextureDimension::Texture3D ? VK_IMAGE_VIEW_TYPE_3D
				: desc.Dimension == ETextureDimension::Texture1D
				? (desc.ArrayLayers == 1 ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_1D_ARRAY)
				: desc.Dimension == ETextureDimension::Cube
				? (desc.ArrayLayers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
				: (desc.ArrayLayers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY);
			view.format = format;
			view.subresourceRange.aspectMask = texture->bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			view.subresourceRange.levelCount = desc.MipLevels;
			view.subresourceRange.layerCount = desc.ArrayLayers;

			if (state.F.CreateImageView(state.Device, &view, nullptr, &texture->View) != VK_SUCCESS)
			{
				FreeTexture(state, texture);

				return Result<void*>::Failure(Failure("Rhi.VulkanTexture", "Could not create a texture image view."));
			}

			if (texture->bStorage && desc.MipLevels > 1)
			{
				view.subresourceRange.levelCount = 1;

				if (state.F.CreateImageView(state.Device, &view, nullptr, &texture->StorageView) != VK_SUCCESS)
				{
					FreeTexture(state, texture);

					return Result<void*>::Failure(Failure("Rhi.VulkanTexture", "Could not create a single-mip storage image view."));
				}
			}
		}

		if (HasUsage(desc.Usage, ETextureUsage::RenderTarget) && desc.MipLevels == 1 && desc.ArrayLayers == 1 && desc.Dimension != ETextureDimension::Texture3D)
		{
			if (desc.SampleCount != 1 || desc.Format != EFormat::RGBA8_UNORM)
			{
				const EFormat colorFormat = desc.Format;
				Result<VkRenderPass> pass = CreateAttachmentRenderPass(state, { &colorFormat, 1 }, EFormat::UNKNOWN, desc.SampleCount);
				if (!pass)
				{
					FreeTexture(state, texture);

					return Result<void*>::Failure(pass.GetError());
				}
				texture->MsaaRenderPass = pass.Value();
				Result<VkRenderPass> depthPass = CreateAttachmentRenderPass(state, { &colorFormat, 1 }, EFormat::D32, desc.SampleCount);
				if (depthPass)
				{
					texture->MsaaDepthRenderPass = depthPass.Value();
					Result<VkRenderPass> loadPass =
						CreateAttachmentRenderPass(state, { &colorFormat, 1 }, EFormat::D32, desc.SampleCount, true);
					if (!loadPass)
					{
						FreeTexture(state, texture);

						return Result<void*>::Failure(loadPass.GetError());
					}
					texture->MsaaDepthLoadRenderPass = loadPass.Value();
				}
			}
			VkFramebufferCreateInfo framebuffer{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			framebuffer.renderPass = texture->MsaaRenderPass != VK_NULL_HANDLE ? texture->MsaaRenderPass : state.RenderPass;
			framebuffer.attachmentCount = 1;
			framebuffer.pAttachments = &texture->View;
			framebuffer.width = desc.Width;
			framebuffer.height = desc.Height;
			framebuffer.layers = 1;

			if (state.F.CreateFramebuffer(state.Device, &framebuffer, nullptr, &texture->Framebuffer) != VK_SUCCESS)
			{
				FreeTexture(state, texture);

				return Result<void*>::Failure(Failure("Rhi.VulkanTexture", "Could not create color attachments."));
			}
		}

		return Result<void*>::Success(texture);
	}

	[[nodiscard]] Result<void*> CreateTexture(State* opaque, const TextureDesc& desc)
	{
		return CreateTextureInternal(opaque, desc, nullptr, 0);
	}

	[[nodiscard]] Result<void*> ImportTexture(State* opaque, const TextureDesc& desc, void* native, ENativeTextureContents contents)
	{
		return CreateTextureInternal(opaque, desc, nullptr, 0, reinterpret_cast<VkImage>(native), contents);
	}

	[[nodiscard]] void* GetNativeTexture(const void* native) noexcept
	{
		return reinterpret_cast<void*>(static_cast<const Texture*>(native)->Handle);
	}

	[[nodiscard]] Result<void*> CreatePlacedTexture(State* opaque, const TextureDesc& desc, void* heap, uint64 offset)
	{
		return CreateTextureInternal(opaque, desc, static_cast<Heap*>(heap), offset);
	}

	[[nodiscard]] uint64 GetTextureLastUse(const void* native) noexcept
	{
		return static_cast<const Texture*>(native)->LastUse;
	}

	void DestroyTextureNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		FreeTexture(state, static_cast<Texture*>(native));
	}

	[[nodiscard]] Result<void> DestroyTexture(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Texture& texture = *static_cast<Texture*>(native);
		uint64 complete = 0;

		if (!ReadCompletedSerial(state, complete))
		{
			return Result<void>::Failure(Failure("Rhi.VulkanTimeline", "Could not inspect Vulkan completion."));
		}

		if (texture.LastUse > complete)
		{
			if (!state.RetiredTextures.Add({ &texture, texture.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track a retired Vulkan texture."));
			}
		}
		else
		{
			FreeTexture(state, &texture);
		}

		return Result<void>::Success();
	}
}
