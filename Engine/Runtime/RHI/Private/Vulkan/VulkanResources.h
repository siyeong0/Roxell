#pragma once

#include "../DeviceCommands.h"
#include "VulkanFunctions.h"

#include <Array.hpp>
#include <RhiInterop.h>
#include <SharedPtr.hpp>
#include <UniquePtr.hpp>

namespace rox::vulkan
{
	using namespace rox::rhi;

	struct Buffer final
	{
		VkBuffer Handle = VK_NULL_HANDLE;

		VkDeviceMemory Memory = VK_NULL_HANDLE;
		VkDeviceSize AllocationSize = 0;

		ExternalHandle ExportableExternalMemory;
		core::uint32 ExternalMemoryType = UINT32_MAX;

		VkDeviceSize ByteCount = 0;
		VkDeviceSize MemoryOffset = 0;

		bool Coherent = false;

		bool bOwnsMemory = true;
		bool bOwnsHandle = true;

		EBufferMemory MemoryAccess = EBufferMemory::Device;

		core::uint64 LastUse = 0;
		EQueue LastQueue = EQueue::Graphics;
	};

	struct Heap final
	{
		VkDeviceMemory Memory = VK_NULL_HANDLE;

		HeapDesc Descriptor;

		core::uint32 MemoryTypeIndex = UINT32_MAX;
		bool Coherent = false;
	};

	struct SwapChain;

	struct Texture final
	{
		explicit Texture(core::MemoryResource& resource) noexcept;

		VkImage Handle = VK_NULL_HANDLE;

		VkDeviceSize ExternalAllocationSize = 0;
		ExternalHandle ExportableExternalMemory;
		core::uint32 ExternalMemoryType = UINT32_MAX;

		VkDeviceMemory Memory = VK_NULL_HANDLE;
		bool bOwnsMemory = true;

		VkImageView View = VK_NULL_HANDLE;
		VkImageView StorageView = VK_NULL_HANDLE;

		[[nodiscard]] VkImageView GetStorageView() const noexcept;

		VkFramebuffer Framebuffer = VK_NULL_HANDLE;
		VkRenderPass MsaaRenderPass = VK_NULL_HANDLE;
		VkRenderPass MsaaDepthRenderPass = VK_NULL_HANDLE;
		VkRenderPass MsaaDepthLoadRenderPass = VK_NULL_HANDLE;

		VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkMemoryRequirements SparseMemoryRequirements{};
		VkSparseImageMemoryRequirements SparseRequirements{};

		core::Array<VkImageLayout> SubresourceLayouts;

		core::uint32 SampleCount = 1;

		EFormat Format = EFormat::UNKNOWN;
		ETextureDimension Dimension = ETextureDimension::Texture2D;

		core::uint32 MipLevels = 1;
		core::uint32 ArrayLayers = 1;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		bool bShaderRead = false;
		bool bStorage = false;
		bool bDepth = false;

		bool bExternalImage = false;
		bool bSparse = false;

		SwapChain* SwapChainOwner = nullptr;
		core::uint32 SwapChainImageIndex = 0;

		core::uint64 LastUse = 0;
		EQueue LastQueue = EQueue::Graphics;
	};

	struct Sampler final
	{
		VkSampler Handle = VK_NULL_HANDLE;
	};

	struct BufferView final
	{
		VkBufferView Handle = VK_NULL_HANDLE;
	};

	struct TextureView final
	{
		VkImageView Handle = VK_NULL_HANDLE;
		TextureViewDesc Descriptor;
	};

	struct DescriptorTable final
	{
		explicit DescriptorTable(core::MemoryResource& resource) noexcept;

		VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
		VkDescriptorPool Pool = VK_NULL_HANDLE;
		VkDescriptorSet Set = VK_NULL_HANDLE;

		core::Array<Texture*> Textures;
		core::Array<TextureView*> TextureViews;
		core::Array<Buffer*> Buffers;

		EDescriptorType Type = EDescriptorType::SampledTexture;
		EFormat BufferFormat = EFormat::UNKNOWN;
		core::uint32 StructureStrideBytes = 0;

		core::uint32 Capacity = 0;
	};

	struct DescriptorSetRealization final
	{
		struct Image final
		{
			Texture* Resource = nullptr;
			TextureView* View = nullptr;

			core::uint32 RangeIndex = 0;
			core::uint32 Element = 0;

			VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
		};

		explicit DescriptorSetRealization(core::MemoryResource& resource) noexcept;

		VkDescriptorPool Pool = VK_NULL_HANDLE;
		VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
		VkDescriptorSet Set = VK_NULL_HANDLE;

		BindingRangeDesc Ranges[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		NativeFixedDescriptor Fixed[32]{};
		void* Tables[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};

		core::Array<Image> Images;

		core::uint32 RangeCount = 0;
		core::uint32 FixedCount = 0;

		core::uint32 Space = 0;

		bool bGraphics = false;
		bool bRay = false;
	};

	struct ComputePipeline final
	{
		NativeFixedLayoutState Fixed;

		VkDescriptorSetLayout SetLayouts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 SetLayoutCount = 0;

		VkDescriptorSetLayout BindlessSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessSamplerSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessStorageBufferSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessReadOnlyBufferSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessStorageTextureSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessUniformBufferSetLayout = VK_NULL_HANDLE;

		VkPipelineLayout Layout = VK_NULL_HANDLE;
		VkPipeline Handle = VK_NULL_HANDLE;
		VkSampler Samplers[MAXIMUM_FIXED_BINDING_LAYOUTS][8]{};
		core::uint32 TextureSlots[MAXIMUM_FIXED_BINDING_LAYOUTS][8]{};

		core::uint32 PushConstantBytes = 0;

		core::uint32 AccelStructSlot = 0xFFFFFFFFu;

		core::uint64 LastUse = 0;
	};

	struct GraphicsPipeline final
	{
		NativeFixedLayoutState Fixed;

		VkDescriptorSetLayout SetLayouts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 SetLayoutCount = 0;

		VkDescriptorSetLayout BindlessSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessSamplerSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessUniformBufferSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout EmptyStorageBufferSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessStorageBufferSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessReadOnlyBufferSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout BindlessStorageTextureSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout EmptyStorageTextureSetLayout = VK_NULL_HANDLE;

		VkPipelineLayout Layout = VK_NULL_HANDLE;
		VkPipeline Handle = VK_NULL_HANDLE;

		VkRenderPass RenderPass = VK_NULL_HANDLE;

		VkPipeline ShadingRateHandle = VK_NULL_HANDLE;
		VkRenderPass ShadingRateRenderPass = VK_NULL_HANDLE;
		VkSampler Samplers[MAXIMUM_FIXED_BINDING_LAYOUTS][8]{};
		core::uint32 SampledTextureSlots[MAXIMUM_FIXED_BINDING_LAYOUTS][8]{};
		core::uint32 SampledSamplerSlots[MAXIMUM_FIXED_BINDING_LAYOUTS][8]{};

		core::uint32 SampledTextureCounts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 StorageTextureSlots[MAXIMUM_FIXED_BINDING_LAYOUTS][8]{};
		core::uint32 StorageTextureCounts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 GraphicsBufferCounts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};

		core::uint32 PushConstantBytes = 0;

		core::uint32 AccelStructSlot = 0xFFFFFFFFu;

		VkShaderStageFlags ShaderStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		bool bMeshShader = false;

		bool bStencil = false;

		bool bBlendConstant = false;

		core::uint64 LastUse = 0;
	};

	struct RayTracingPipeline final
	{
		NativeFixedLayoutState Fixed;

		explicit RayTracingPipeline(core::MemoryResource& resource) noexcept;

		core::Array<core::Byte> GroupHandles;

		core::uint32 LocalDataAlignmentBytes = 1;

		VkDescriptorSetLayout SetLayouts[5]{};
		VkSampler LocalSamplers[64][8]{};
		core::uint32 LocalDescriptorCounts[5]{};
		core::uint32 SetLayoutCount = 0;

		VkPipelineLayout Layout = VK_NULL_HANDLE;
		VkPipeline Handle = VK_NULL_HANDLE;
		VkSampler Samplers[4][8]{};
		core::uint32 TextureSlots[4][8]{};

		core::uint32 AccelStructSlot = 0xFFFFFFFFu;
		core::uint32 PushConstantBytes = 0;

		core::uint64 LastUse = 0;
	};

	struct Framebuffer final
	{
		VkRenderPass RenderPass = VK_NULL_HANDLE;
		VkRenderPass DepthLoadRenderPass = VK_NULL_HANDLE;
		VkRenderPass DepthReadOnlyRenderPass = VK_NULL_HANDLE;
		VkRenderPass MixedDepthStencilRenderPasses[2]{};

		VkFramebuffer Handle = VK_NULL_HANDLE;

		VkImageView AttachmentViews[10]{};

		Texture* Colors[8]{};
		core::uint32 ColorSubresources[8]{};

		Texture* Depth = nullptr;

		Texture* ShadingRateImage = nullptr;
		core::uint32 DepthSubresource = 0;

		core::uint32 ColorCount = 0;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		core::uint64 LastUse = 0;

		bool bOwnRenderPass = false;
	};

	struct SwapChain final
	{
		VkSurfaceKHR Surface = VK_NULL_HANDLE;
		SwapChain* Next = nullptr;

		VkFence AcquireFences[3]{};
		bool bAcquirePending[3]{};

		VkFence PresentFences[8]{};
		bool bPresentPending[8]{};
		bool bUnboundedPresent = false;

		EFormat Format = EFormat::RGBA8_UNORM;
		EColorSpace ColorSpace = EColorSpace::SrgbNonlinear;

		bool bEnableHdrMetadata = false;
		HdrMetadata Metadata;

		VkSwapchainKHR Native = VK_NULL_HANDLE;

		VkImage Images[8]{};
		Texture* Targets[8]{};

		bool bUsable = true;
		bool bDirectRendering = false;

		bool bAcquired = false;
		bool bReady = false;
		bool bAcquireConsumed = false;

		core::uint32 AcquiredImageIndex = 0;
		core::uint32 AcquiredSemaphoreIndex = 0;

		core::uint64 SubmittedSerial = 0;

		VkSemaphore AcquireSemaphores[3]{};
		VkSemaphore PresentSemaphores[8]{};

		core::uint64 AcquireSerials[3]{};
		bool bPresented[8]{};

		core::uint32 ImageCount = 0;
		core::uint32 BufferCount = 0;
		core::uint32 NextAcquire = 0;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		bool bVSync = true;
	};
}
