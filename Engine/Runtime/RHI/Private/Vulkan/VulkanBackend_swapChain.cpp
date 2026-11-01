#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	void FreeSwapChainTargets(State& state, SwapChain& swapChain) noexcept
	{
		for (Texture*& target : swapChain.Targets)
		{
			if (target)
			{
				FreeTexture(state, target);
				target = nullptr;
			}
		}
	}

	void FreeSwapChain(State& state, SwapChain* swapChain) noexcept
	{
		if (swapChain == nullptr)
		{
			return;
		}
		SwapChain** link = &state.SwapChains;

		while (*link != nullptr && *link != swapChain)
		{
			link = &(*link)->Next;
		}

		if (*link == swapChain)
		{
			*link = swapChain->Next;
		}

		for (VkFence fence : swapChain->AcquireFences)
		{
			if (fence)
			{
				state.F.DestroyFence(state.Device, fence, nullptr);
			}
		}

		for (const VkFence fence : swapChain->PresentFences)
		{
			if (fence)
			{
				state.F.DestroyFence(state.Device, fence, nullptr);
			}
		}
		FreeSwapChainTargets(state, *swapChain);

		for (VkSemaphore semaphore : swapChain->AcquireSemaphores)
		{
			if (semaphore)
			{
				state.F.DestroySemaphore(state.Device, semaphore, nullptr);
			}
		}

		for (VkSemaphore semaphore : swapChain->PresentSemaphores)
		{
			if (semaphore)
			{
				state.F.DestroySemaphore(state.Device, semaphore, nullptr);
			}
		}

		if (swapChain->Native)
		{
			state.F.DestroySwapchain(state.Device, swapChain->Native, nullptr);
		}

		if (swapChain->Surface)
		{
			state.F.DestroySurface(state.Instance, swapChain->Surface, nullptr);
		}

		DestroyObject(state.Resource, swapChain);
	}

	[[nodiscard]] Result<void> ResetSwapChainSemaphores(State& state, SwapChain& swapChain)
	{
		VkSemaphore acquire[3]{};
		VkSemaphore present[8]{};
		VkSemaphoreCreateInfo info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		bool bCreated = true;

		for (uint32 index = 0; index < 8; ++index)
		{
			if ((index < swapChain.BufferCount && state.F.CreateSemaphore(state.Device, &info, nullptr, &acquire[index]) != VK_SUCCESS) ||
				state.F.CreateSemaphore(state.Device, &info, nullptr, &present[index]) != VK_SUCCESS)
			{
				bCreated = false;
				break;
			}
		}

		if (!bCreated)
		{
			for (VkSemaphore semaphore : acquire)
			{
				if (semaphore)
				{
					state.F.DestroySemaphore(state.Device, semaphore, nullptr);
				}
			}

			for (VkSemaphore semaphore : present)
			{
				if (semaphore)
				{
					state.F.DestroySemaphore(state.Device, semaphore, nullptr);
				}
			}

			return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not create presentation semaphores."));
		}

		for (uint32 index = 0; index < 3; ++index)
		{
			if (swapChain.AcquireSemaphores[index])
			{
				state.F.DestroySemaphore(state.Device, swapChain.AcquireSemaphores[index], nullptr);
			}
			swapChain.AcquireSemaphores[index] = acquire[index];
			swapChain.AcquireSerials[index] = 0;
		}

		for (uint32 index = 0; index < 8; ++index)
		{
			if (swapChain.PresentSemaphores[index])
			{
				state.F.DestroySemaphore(state.Device, swapChain.PresentSemaphores[index], nullptr);
			}
			swapChain.PresentSemaphores[index] = present[index];
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> CreateNativeSwapChain(State& state, SwapChain& swapChain, uint32 width, uint32 height)
	{
		VkSurfaceCapabilitiesKHR capabilities{};
		uint32 formatCount = 0;
		uint32 modeCount = 0;

		if (state.F.GetSurfaceCapabilities(state.Physical, swapChain.Surface, &capabilities) != VK_SUCCESS ||
			state.F.GetSurfaceFormats(state.Physical, swapChain.Surface, &formatCount, nullptr) != VK_SUCCESS ||
			state.F.GetPresentModes(state.Physical, swapChain.Surface, &modeCount, nullptr) != VK_SUCCESS ||
			formatCount == 0 ||
			modeCount == 0)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not query the window surface."));
		}

		Array<VkSurfaceFormatKHR> formats(state.Resource);
		Array<VkPresentModeKHR> modes(state.Resource);

		if (!formats.ResizeUninitialized(formatCount) || !modes.ResizeUninitialized(modeCount))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not enumerate window surface formats."));
		}

		if (state.F.GetSurfaceFormats(state.Physical, swapChain.Surface, &formatCount, formats.Data()) != VK_SUCCESS ||
			state.F.GetPresentModes(state.Physical, swapChain.Surface, &modeCount, modes.Data()) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not read window surface formats."));
		}

		const VkFormat requestedFormat = ConvertVulkanFormat(swapChain.Format);
		const VkColorSpaceKHR requestedColorSpace =
			swapChain.ColorSpace == EColorSpace::LinearSrgb ? VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT
			: swapChain.ColorSpace == EColorSpace::Hdr10PqRec2020 ? VK_COLOR_SPACE_HDR10_ST2084_EXT
			: VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		bool bRgba = false;

		for (const VkSurfaceFormatKHR& format : formats)
		{
			if (format.format == requestedFormat && format.colorSpace == requestedColorSpace)
			{
				bRgba = true;
				break;
			}

			if (format.format == VK_FORMAT_UNDEFINED && format.colorSpace == requestedColorSpace)
			{
				bRgba = true;
				break;
			}
		}

		if (!bRgba ||
			(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0 ||
			capabilities.minImageCount > swapChain.BufferCount ||
			(capabilities.maxImageCount != 0 && capabilities.maxImageCount < swapChain.BufferCount))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanPresent", "The requested surface format, color space, copy usage, or image count is unavailable.");
		}

		if (capabilities.currentExtent.width != NumericLimits<uint32>::max() && (capabilities.currentExtent.width != width || capabilities.currentExtent.height != height))
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanPresent", "The swapchain extent differs from the window client area.");
		}

		if (width < capabilities.minImageExtent.width || height < capabilities.minImageExtent.height || width> capabilities.maxImageExtent.width || height> capabilities.maxImageExtent.height)
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanPresent", "The swapchain extent is outside surface limits.");
		}
		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

		if (!swapChain.bVSync)
		{
			for (VkPresentModeKHR mode : modes)
			{
				if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					presentMode = mode;
					break;
				}

				if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
				{
					presentMode = mode;
				}
			}
		}
		VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

		if ((capabilities.supportedCompositeAlpha & alpha) == 0)
		{
			const VkCompositeAlphaFlagBitsKHR candidates[]{ VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
														   VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR };
			bool bFound = false;

			for (VkCompositeAlphaFlagBitsKHR candidate : candidates)
			{
				if (capabilities.supportedCompositeAlpha & candidate)
				{
					alpha = candidate;
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanPresent", "The surface has no supported alpha mode.");
			}
		}
		VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		info.surface = swapChain.Surface;
		info.minImageCount = swapChain.BufferCount;
		info.imageFormat = requestedFormat;
		info.imageColorSpace = requestedColorSpace;
		info.imageExtent = { width, height };
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		const bool bDirectRendering = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;

		if (bDirectRendering)
		{
			info.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.preTransform = capabilities.currentTransform;
		info.compositeAlpha = alpha;
		info.presentMode = presentMode;
		info.clipped = VK_TRUE;
		info.oldSwapchain = swapChain.Native;
		VkSwapchainKHR replacement = VK_NULL_HANDLE;

		if (state.F.CreateSwapchain(state.Device, &info, nullptr, &replacement) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not create the Vulkan window swapchain."));
		}

		uint32 imageCount = 0;
		VkImage images[8]{};
		if (state.F.GetSwapchainImages(state.Device, replacement, &imageCount, nullptr) != VK_SUCCESS ||
			imageCount < 2 ||
			imageCount> 8 ||
			state.F.GetSwapchainImages(state.Device, replacement, &imageCount, images) != VK_SUCCESS)
		{
			FreeSwapChainTargets(state, swapChain);

			if (swapChain.Native)
			{
				state.F.DestroySwapchain(state.Device, swapChain.Native, nullptr);
			}
			swapChain.Native = replacement;
			swapChain.ImageCount = 0;
			swapChain.bUsable = false;

			return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not access Vulkan swapchain images."));
		}
		Texture* targets[8]{};
		bool bBuilt = true;

		if (bDirectRendering)
		{
			for (uint32 index = 0; index < imageCount; ++index)
			{
				Texture* target = CreateObject<Texture>(state.Resource, state.Resource);

				if (target == nullptr)
				{
					bBuilt = false;
					break;
				}
				targets[index] = target;
				target->Handle = images[index];
				target->bExternalImage = true;
				target->Format = swapChain.Format;
				target->SwapChainOwner = &swapChain;
				target->SwapChainImageIndex = index;
				VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
				view.image = images[index];
				view.viewType = VK_IMAGE_VIEW_TYPE_2D;
				view.format = requestedFormat;
				view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				view.subresourceRange.levelCount = 1;
				view.subresourceRange.layerCount = 1;

				if (state.F.CreateImageView(state.Device, &view, nullptr, &target->View) != VK_SUCCESS)
				{
					bBuilt = false;
					break;
				}
				VkFramebufferCreateInfo framebuffer{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
				if (swapChain.Format != EFormat::RGBA8_UNORM)
				{
					Result<VkRenderPass> renderPass = CreateAttachmentRenderPass(state, { &swapChain.Format, 1 }, EFormat::UNKNOWN, 1);
					if (!renderPass)
					{
						bBuilt = false;
						break;
					}
					target->MsaaRenderPass = renderPass.Value();
				}
				framebuffer.renderPass = target->MsaaRenderPass != VK_NULL_HANDLE ? target->MsaaRenderPass : state.RenderPass;
				framebuffer.attachmentCount = 1;
				framebuffer.pAttachments = &target->View;
				framebuffer.width = width;
				framebuffer.height = height;
				framebuffer.layers = 1;

				if (state.F.CreateFramebuffer(state.Device, &framebuffer, nullptr, &target->Framebuffer) != VK_SUCCESS)
				{
					bBuilt = false;
					break;
				}
			}
		}

		if (!bBuilt)
		{
			for (Texture* target : targets)
			{
				if (target)
				{
					FreeTexture(state, target);
				}
			}
			FreeSwapChainTargets(state, swapChain);

			if (swapChain.Native)
			{
				state.F.DestroySwapchain(state.Device, swapChain.Native, nullptr);
			}
			swapChain.Native = replacement;
			swapChain.ImageCount = 0;
			swapChain.bUsable = false;

			return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not create Vulkan swapchain render targets."));
		}
		FreeSwapChainTargets(state, swapChain);

		if (swapChain.Native)
		{
			state.F.DestroySwapchain(state.Device, swapChain.Native, nullptr);
		}
		swapChain.Native = replacement;

		if (swapChain.bEnableHdrMetadata)
		{
			const HdrMetadata& source = swapChain.Metadata;
			VkHdrMetadataEXT metadata{ VK_STRUCTURE_TYPE_HDR_METADATA_EXT };
			metadata.displayPrimaryRed = { source.RedPrimary[0], source.RedPrimary[1] };
			metadata.displayPrimaryGreen = { source.GreenPrimary[0], source.GreenPrimary[1] };
			metadata.displayPrimaryBlue = { source.BluePrimary[0], source.BluePrimary[1] };
			metadata.whitePoint = { source.WhitePoint[0], source.WhitePoint[1] };
			metadata.maxLuminance = source.MaximumLuminance;
			metadata.minLuminance = source.MinimumLuminance;
			metadata.maxContentLightLevel = source.MaximumContentLightLevel;
			metadata.maxFrameAverageLightLevel = source.MaximumFrameAverageLightLevel;
			state.F.SetHdrMetadata(state.Device, 1, &replacement, &metadata);
		}
		swapChain.bUsable = true;
		swapChain.bDirectRendering = bDirectRendering;
		swapChain.bAcquired = false;
		swapChain.bReady = false;
		swapChain.bAcquireConsumed = false;
		swapChain.SubmittedSerial = 0;
		swapChain.ImageCount = imageCount;
		swapChain.Width = width;
		swapChain.Height = height;

		for (uint32 index = 0; index < 8; ++index)
		{
			swapChain.Images[index] = images[index];
			swapChain.Targets[index] = targets[index];
			swapChain.bPresented[index] = false;
		}

		for (uint32 index = 0; index < 3; ++index)
		{
			swapChain.AcquireSerials[index] = 0;
		}
		swapChain.NextAcquire = 0;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreateSwapChain(State* opaque, const SwapChainDesc& desc)
	{
		State& state = *opaque;

		if (!state.bSurfaceEnabled || !state.bSwapchainEnabled)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanPresent", "This Vulkan device has no window presentation support.");
		}

		if ((desc.ColorSpace != EColorSpace::SrgbNonlinear && !state.bExtendedColorSpace) || (desc.bEnableHdrMetadata && state.F.SetHdrMetadata == nullptr))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanPresent", "The requested HDR presentation extensions are unavailable.");
		}
		SwapChain* swapChain = CreateObject<SwapChain>(state.Resource);

		if (swapChain == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a swapchain record."));
		}
		swapChain->Next = state.SwapChains;
		state.SwapChains = swapChain;
		swapChain->BufferCount = desc.BufferCount;
		swapChain->bVSync = desc.bVSync;
		swapChain->Format = desc.Format;
		swapChain->ColorSpace = desc.ColorSpace;
		swapChain->bEnableHdrMetadata = desc.bEnableHdrMetadata;
		swapChain->Metadata = desc.Metadata;
		VkResult surfaceResult = VK_ERROR_EXTENSION_NOT_PRESENT;
#ifdef _WIN32
		if (desc.WindowKind == ENativeWindowKind::Win32 && state.F.CreateWin32Surface != nullptr && IsWindow(reinterpret_cast<HWND>(desc.Window)))
		{
			VkWin32SurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
			info.hinstance = GetModuleHandleW(nullptr);
			info.hwnd = reinterpret_cast<HWND>(desc.Window);
			surfaceResult = state.F.CreateWin32Surface(state.Instance, &info, nullptr, &swapChain->Surface);
		}
#elif defined(__ANDROID__)
		if (desc.WindowKind == ENativeWindowKind::Android && state.F.CreateAndroidSurface != nullptr)
		{
			VkAndroidSurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
			info.window = reinterpret_cast<ANativeWindow*>(desc.Window);
			surfaceResult = state.F.CreateAndroidSurface(state.Instance, &info, nullptr, &swapChain->Surface);
		}
#elif defined(__linux__)
		if (desc.WindowKind == ENativeWindowKind::X11 && desc.Display != 0 && state.F.CreateXlibSurface != nullptr)
		{
			VkXlibSurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
			info.dpy = reinterpret_cast<Display*>(desc.Display);
			info.window = static_cast<Window>(desc.Window);
			surfaceResult = state.F.CreateXlibSurface(state.Instance, &info, nullptr, &swapChain->Surface);
		}
		else if (desc.WindowKind == ENativeWindowKind::Wayland && desc.Display != 0 && state.F.CreateWaylandSurface != nullptr)
		{
			VkWaylandSurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
			info.display = reinterpret_cast<wl_display*>(desc.Display);
			info.surface = reinterpret_cast<wl_surface*>(desc.Window);
			surfaceResult = state.F.CreateWaylandSurface(state.Instance, &info, nullptr, &swapChain->Surface);
		}
#endif
		if (surfaceResult != VK_SUCCESS)
		{
			FreeSwapChain(state, swapChain);

			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanPresent", "Could not create the requested native window surface.");
		}
		VkBool32 supported = VK_FALSE;

		if (state.F.GetSurfaceSupport(state.Physical, state.QueueFamily, swapChain->Surface, &supported) != VK_SUCCESS || supported != VK_TRUE)
		{
			FreeSwapChain(state, swapChain);

			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanPresent", "The Vulkan graphics queue cannot present to this window.");
		}

		Result<void> created = CreateNativeSwapChain(state, *swapChain, desc.Width, desc.Height);

		if (!created)
		{
			FreeSwapChain(state, swapChain);

			return Result<void*>::Failure(created.GetError());
		}

		Result<void> semaphores = ResetSwapChainSemaphores(state, *swapChain);

		if (!semaphores)
		{
			FreeSwapChain(state, swapChain);

			return Result<void*>::Failure(semaphores.GetError());
		}

		return Result<void*>::Success(swapChain);
	}

	[[nodiscard]] Result<void> ResizeSwapChainWithPolicy(State* opaque, void* native, uint32 width, uint32 height, bool bPoll)
	{
		State& state = *opaque;
		Result<void> idle = bPoll ? WaitIdleBounded(&state, 0) : WaitIdle(&state);

		if (!idle)
		{
			return idle;
		}

		SwapChain& swapChain = *static_cast<SwapChain*>(native);
		Result<void> resized = CreateNativeSwapChain(state, swapChain, width, height);

		if (!resized)
		{
			return resized;
		}

		Result<void> semaphores = ResetSwapChainSemaphores(state, swapChain);

		if (!semaphores)
		{
			swapChain.bUsable = false;
		}

		return semaphores;
	}

	[[nodiscard]] Result<void> ResizeSwapChain(State* opaque, void* native, uint32 width, uint32 height)
	{
		return ResizeSwapChainWithPolicy(opaque, native, width, height, false);
	}

	[[nodiscard]] Result<void> TryResizeSwapChain(State* opaque, void* native, uint32 width, uint32 height)
	{
		return ResizeSwapChainWithPolicy(opaque, native, width, height, true);
	}

	[[nodiscard]] Result<void> PrepareAcquireFence(State& state, SwapChain& swapChain, uint32 index, uint32 timeoutMilliseconds)
	{
		if (swapChain.bAcquirePending[index])
		{
			Result<void> waited = WaitNativeFence(state, swapChain.AcquireFences[index], static_cast<uint64>(timeoutMilliseconds) * NANOSECONDS_PER_MILLISECOND);

			if (!waited)
			{
				return waited;
			}
			swapChain.bAcquirePending[index] = false;
		}

		if (!swapChain.AcquireFences[index])
		{
			VkFenceCreateInfo info{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			if (state.F.CreateFence(state.Device, &info, nullptr, &swapChain.AcquireFences[index]) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not create the acquire fence."));
			}
		}
		else if (state.F.ResetFences(state.Device, 1, &swapChain.AcquireFences[index]) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not reset the acquire fence."));
		}
		InvalidateIdle(state);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> PreparePresentFence(State& state, SwapChain& swapChain, VkPresentInfoKHR& present, VkSwapchainPresentFenceInfoEXT& info)
	{
		if (!state.bSwapchainMaintenance)
		{
			return Result<void>::Success();
		}

		const uint32_t index = *present.pImageIndices;
		// Reacquisition retired the previous presentation of this exact image. Normal
		// Present must not wait on a different frame or use draw completion as WSI proof.
		ASSERT(index < swapChain.ImageCount && !swapChain.bPresentPending[index], "The acquired image's prior presentation fence is retired before reuse.");
		if (!swapChain.PresentFences[index])
		{
			VkFenceCreateInfo create{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			if (state.F.CreateFence(state.Device, &create, nullptr, &swapChain.PresentFences[index]) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not create the presentation fence."));
			}
		}
		else if (state.F.ResetFences(state.Device, 1, &swapChain.PresentFences[index]) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanPresent", "Could not reset the presentation fence."));
		}
		info.swapchainCount = 1;
		info.pFences = &swapChain.PresentFences[index];
		info.pNext = present.pNext;
		present.pNext = &info;

		return Result<void>::Success();
	}

	void ObservePresentResult(State& state, SwapChain& swapChain, uint32 imageIndex, VkResult result) noexcept
	{
		InvalidateIdle(state);
		state.bDeviceLossConfirmed |= result == VK_ERROR_DEVICE_LOST;
		// OOM leaves synchronization payloads unchanged. Out-of-date and surface-lost
		// still enqueue semaphore waits and the present completion fence.
		const bool bQueued = result != VK_ERROR_OUT_OF_HOST_MEMORY && result != VK_ERROR_OUT_OF_DEVICE_MEMORY;
		swapChain.bPresentPending[imageIndex] = bQueued && state.bSwapchainMaintenance;
		swapChain.bUnboundedPresent |= bQueued && !state.bSwapchainMaintenance;
	}

	[[nodiscard]] Result<void> RetireImagePresentation(State& state, SwapChain& swapChain, uint32 index, uint32 timeoutMilliseconds)
	{
		if (!swapChain.bPresentPending[index])
		{
			return Result<void>::Success();
		}
		Result<void> retired = WaitNativeFence(state, swapChain.PresentFences[index], uint64(timeoutMilliseconds) * NANOSECONDS_PER_MILLISECOND);
		if (!retired)
		{
			swapChain.bAcquired = false;
			swapChain.bReady = false;
			swapChain.bUsable = false;

			return retired;
		}
		swapChain.bPresentPending[index] = false;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> DiscardAcquiredSwapChainImage(State* opaque, void* native)
	{
		(void)opaque;
		SwapChain& swapChain = *static_cast<SwapChain*>(native);
		// Core has joined accepted work and released all recorded/retained image users.
		// Abandonment grants no layout/content or presentation completion proof.
		swapChain.bAcquired = false;
		swapChain.bReady = false;
		swapChain.bUsable = false;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> AcquireSwapChainImage(State* opaque, void* native, uint32 timeoutMilliseconds)
	{
		State& state = *opaque;
		SwapChain& swapChain = *static_cast<SwapChain*>(native);

		if (state.bLost || !swapChain.bUsable || swapChain.bAcquired)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanPresent", "The swapchain image is unavailable."));
		}

		if (!swapChain.bDirectRendering)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanPresent", "The surface cannot render directly to swapchain images.");
		}

		const uint32 acquireIndex = swapChain.NextAcquire % swapChain.BufferCount;

		if (swapChain.AcquireSerials[acquireIndex] != 0)
		{
			Result<void> waited = Wait(&state, EQueue::Graphics, swapChain.AcquireSerials[acquireIndex], timeoutMilliseconds);

			if (!waited)
			{
				return Result<void*>::Failure(waited.GetError());
			}
		}

		Result<void> fenceReady = PrepareAcquireFence(state, swapChain, acquireIndex, timeoutMilliseconds);

		if (!fenceReady)
		{
			return Result<void*>::Failure(fenceReady.GetError());
		}

		uint32 imageIndex = 0;
		VkResult result;
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
		if (bReturnNotReadyOnNextAcquire)
		{
			ASSERT(timeoutMilliseconds == 0, "The not-ready acquire seam requires a nonblocking call.");
			bReturnNotReadyOnNextAcquire = false;
			result = VK_NOT_READY;
		}
		else
#endif
		{
			result = state.F.AcquireNextImage(
				state.Device, swapChain.Native, static_cast<uint64>(timeoutMilliseconds) * NANOSECONDS_PER_MILLISECOND,
				swapChain.AcquireSemaphores[acquireIndex], swapChain.AcquireFences[acquireIndex], &imageIndex);
		}
		swapChain.bAcquirePending[acquireIndex] = result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
		state.bDeviceLossConfirmed |= result == VK_ERROR_DEVICE_LOST;

		if (result == VK_TIMEOUT || result == VK_NOT_READY)
		{
			return Result<void*>::Failure(EErrorCode::Timeout, "Rhi.Timeout", "The swapchain image acquisition timed out.");
		}

		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			swapChain.bUsable = false;
			return Result<void*>::Failure(EErrorCode::Conflict, "Rhi.RecreateRequired", "The swapchain must be recreated.");
		}

		if ((result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) || imageIndex >= swapChain.ImageCount || swapChain.Targets[imageIndex] == nullptr)
		{
			swapChain.bUsable = false;

			return Result<void*>::Failure(Failure("Rhi.VulkanPresent", "Could not acquire a renderable swapchain image."));
		}
		Result<void> presentationRetired = RetireImagePresentation(state, swapChain, imageIndex, timeoutMilliseconds);
		if (!presentationRetired)
		{
			swapChain.AcquiredImageIndex = imageIndex;
			swapChain.AcquiredSemaphoreIndex = acquireIndex;
			swapChain.bAcquired = true;
			swapChain.bUsable = false;
			return Result<void*>::Failure(EErrorCode::ExternalFailure, "Rhi.AcquireOwnership", "An acquired image could not establish presentation retirement.");
		}
		swapChain.AcquiredImageIndex = imageIndex;
		swapChain.AcquiredSemaphoreIndex = acquireIndex;
		swapChain.bAcquired = true;
		swapChain.bReady = false;
		swapChain.bAcquireConsumed = false;
		++swapChain.NextAcquire;

		return Result<void*>::Success(swapChain.Targets[imageIndex]);
	}

	[[nodiscard]] Result<uint64> PresentSwapChainImage(State* opaque, void* native)
	{
		State& state = *opaque;
		state.PresentCopySerial = 0;
		SwapChain& swapChain = *static_cast<SwapChain*>(native);

		if (state.bLost || !swapChain.bUsable || !swapChain.bAcquired)
		{
			if (swapChain.bAcquired)
			{
				swapChain.bAcquired = false;
				swapChain.bReady = false;
				swapChain.bUsable = false;
			}

			return Result<uint64>::Failure(Failure("Rhi.VulkanPresent", "The acquired image is unavailable."));
		}

		bool bSucceeded = false;
		ScopeExit closeFailedAcquisition(
			[&]() noexcept
			{
				if (!bSucceeded)
				{
					swapChain.bAcquired = false;
					swapChain.bReady = false;
					swapChain.bUsable = false;
				}
			});
		const uint32 imageIndex = swapChain.AcquiredImageIndex;
		const uint32 acquireIndex = swapChain.AcquiredSemaphoreIndex;

		if (!swapChain.bReady)
		{
			ASSERT(!swapChain.bAcquireConsumed, "Prepared image work has not completed its terminal release submission.");
			Result<Pending> started = Begin(state, EQueue::Graphics);

			if (!started)
			{
				return Result<uint64>::Failure(started.GetError());
			}
			Pending pending = Move(started).TakeValue();
			Texture& image = *swapChain.Targets[imageIndex];

			if (image.Layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
			{
				RequireTextureLayout(state, pending, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
			}

			Result<uint64> submitted = Execute(state, Move(pending), swapChain.AcquireSemaphores[acquireIndex],
				swapChain.PresentSemaphores[imageIndex], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

			if (!submitted)
			{
				swapChain.bUsable = false;

				return submitted;
			}
			image.Layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			image.LastUse = submitted.Value();
			image.LastQueue = EQueue::Graphics;
			swapChain.AcquireSerials[acquireIndex] = submitted.Value();
			swapChain.bAcquireConsumed = true;
			swapChain.SubmittedSerial = submitted.Value();
			swapChain.bReady = true;
		}
		VkPresentInfoKHR info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &swapChain.PresentSemaphores[imageIndex];
		info.swapchainCount = 1;
		info.pSwapchains = &swapChain.Native;
		info.pImageIndices = &imageIndex;
		VkSwapchainPresentFenceInfoEXT fenceInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT };
		Result<void> prepared = PreparePresentFence(state, swapChain, info, fenceInfo);

		if (!prepared)
		{
			return Result<uint64>::Failure(prepared.GetError());
		}

		const VkResult shown = state.F.QueuePresent(state.Queue, &info);
		state.PresentCopySerial = swapChain.SubmittedSerial;
		ObservePresentResult(state, swapChain, imageIndex, shown);
		swapChain.bAcquired = false;
		swapChain.bReady = false;

		if (shown != VK_SUCCESS && shown != VK_SUBOPTIMAL_KHR)
		{
			swapChain.bUsable = false;
			if (shown == VK_ERROR_OUT_OF_DATE_KHR)
			{
				return Result<uint64>::Failure(EErrorCode::Conflict, "Rhi.RecreateRequired", "The swapchain must be recreated.");
			}

			return Result<uint64>::Failure(Failure("Rhi.VulkanPresent", "Could not present the rendered swapchain image."));
		}
		swapChain.bPresented[imageIndex] = true;
		bSucceeded = true;

		return Result<uint64>::Success(swapChain.SubmittedSerial);
	}

	[[nodiscard]] Result<uint64> Present(State* opaque, void* native, void* sourceNative)
	{
		State& state = *opaque;
		state.PresentCopySerial = 0;
		state.bSubmissionMayBePending = false;

		if (state.bLost)
		{
			return Result<uint64>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		SwapChain& swapChain = *static_cast<SwapChain*>(native);
		Texture& source = *static_cast<Texture*>(sourceNative);

		if (!swapChain.bUsable || swapChain.bAcquired)
		{
			return Result<uint64>::Failure(Failure("Rhi.VulkanPresent", "The swapchain needs to be resized or recreated."));
		}

		const uint32 acquireIndex = swapChain.NextAcquire % swapChain.BufferCount;

		if (swapChain.AcquireSerials[acquireIndex] != 0)
		{
			Result<void> waited = Wait(&state, EQueue::Graphics, swapChain.AcquireSerials[acquireIndex], 10000);

			if (!waited)
			{
				return Result<uint64>::Failure(waited.GetError());
			}
		}

		Result<void> fenceReady = PrepareAcquireFence(state, swapChain, acquireIndex, 5000);

		if (!fenceReady)
		{
			return Result<uint64>::Failure(fenceReady.GetError());
		}

		Result<Pending> started = Begin(state, EQueue::Graphics);

		if (!started)
		{
			return Result<uint64>::Failure(started.GetError());
		}
		Pending pending = Move(started).TakeValue();
		uint32 imageIndex = 0;
		VkResult acquired = state.F.AcquireNextImage(state.Device, swapChain.Native, 1000000000ull, swapChain.AcquireSemaphores[acquireIndex], swapChain.AcquireFences[acquireIndex], &imageIndex);
		swapChain.bAcquirePending[acquireIndex] = acquired == VK_SUCCESS || acquired == VK_SUBOPTIMAL_KHR;
		state.bDeviceLossConfirmed |= acquired == VK_ERROR_DEVICE_LOST;

		if ((acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) || imageIndex >= swapChain.ImageCount)
		{
			state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);
			swapChain.bUsable = false;
			if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
			{
				return Result<uint64>::Failure(EErrorCode::Conflict, "Rhi.RecreateRequired", "The swapchain must be recreated.");
			}
			return
				Result<uint64>::Failure(Failure("Rhi.VulkanPresent", "Could not acquire a window swapchain image."));
		}
		Result<void> presentationRetired = RetireImagePresentation(state, swapChain, imageIndex, 5000);
		if (!presentationRetired)
		{
			state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);

			return Result<uint64>::Failure(presentationRetired.GetError());
		}
		RequireTextureLayout(state, pending, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		VkImageMemoryBarrier imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		imageBarrier.srcAccessMask = 0;
		imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrier.oldLayout = swapChain.bPresented[imageIndex] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.image = swapChain.Images[imageIndex];
		imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.layerCount = 1;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
			&imageBarrier);
		VkImageCopy copy{};
		copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.srcSubresource.layerCount = 1;
		copy.dstSubresource = copy.srcSubresource;
		copy.extent = { swapChain.Width, swapChain.Height, 1 };
		state.F.CmdCopyImage(pending.Commands, source.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChain.Images[imageIndex],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
		imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrier.dstAccessMask = 0;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
			&imageBarrier);
		const VkImageLayout sourceLayout = source.bShaderRead || source.bStorage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		if (sourceLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			RequireTextureLayout(state, pending, source, sourceLayout);
		}

		Result<uint64> submitted = Execute(state, Move(pending), swapChain.AcquireSemaphores[acquireIndex], swapChain.PresentSemaphores[imageIndex]);

		if (!submitted)
		{
			state.bLost = true;

			return submitted;
		}
		source.Layout = sourceLayout;
		state.PresentCopySerial = submitted.Value();
		source.LastUse = submitted.Value();
		source.LastQueue = EQueue::Graphics;
		swapChain.AcquireSerials[acquireIndex] = submitted.Value();
		++swapChain.NextAcquire;
		swapChain.bPresented[imageIndex] = true;

		if (swapChain.Targets[imageIndex])
		{
			swapChain.Targets[imageIndex]->Layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}
		VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &swapChain.PresentSemaphores[imageIndex];
		present.swapchainCount = 1;
		present.pSwapchains = &swapChain.Native;
		present.pImageIndices = &imageIndex;
		VkSwapchainPresentFenceInfoEXT fenceInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT };
		Result<void> prepared = PreparePresentFence(state, swapChain, present, fenceInfo);

		if (!prepared)
		{
			return Result<uint64>::Failure(prepared.GetError());
		}
		VkResult shown = state.F.QueuePresent(state.Queue, &present);
		ObservePresentResult(state, swapChain, imageIndex, shown);

		if (shown != VK_SUCCESS && shown != VK_SUBOPTIMAL_KHR)
		{
			swapChain.bUsable = false;
			if (shown == VK_ERROR_OUT_OF_DATE_KHR)
			{
				return Result<uint64>::Failure(EErrorCode::Conflict, "Rhi.RecreateRequired", "The swapchain must be recreated.");
			}

			return Result<uint64>::Failure(Failure("Rhi.VulkanPresent", "Could not present the window swapchain image."));
		}

		return submitted;
	}

	void DestroySwapChainNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		FreeSwapChain(state, static_cast<SwapChain*>(native));
	}

	[[nodiscard]] Result<void> DestroySwapChain(State* opaque, void* native)
	{
		Result<void> idle = WaitIdle(opaque);

		if (!idle)
		{
			return idle;
		}
		DestroySwapChainNow(opaque, native);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> TryDestroySwapChain(State* opaque, void* native)
	{
		Result<void> idle = WaitIdleBounded(opaque, 0);
		if (!idle)
		{
			return idle;
		}
		DestroySwapChainNow(opaque, native);

		return Result<void>::Success();
	}
}
