#pragma once

#include "../DevicePipelines.h"
#include "../PreparedProgramCache.h"
#include "VulkanResources.h"

namespace rox::vulkan
{
	using namespace rox::rhi;

	struct State;

	struct PreparedRayTableOwner final
	{
		PreparedRayTableOwner() noexcept = default;
		~PreparedRayTableOwner() noexcept;
		State* Device = nullptr;
		Buffer* Resource = nullptr;
		VkStridedDeviceAddressRegionKHR Rays[4]{};
	};

	struct PreparedAccelQueryOwner final
	{
		PreparedAccelQueryOwner() noexcept = default;
		~PreparedAccelQueryOwner() noexcept;

		core::SharedPtr<PreparedAccelQueryOwner> Previous;
		State* Device = nullptr;
		core::uint64 Generation = 0;

		core::uint64 Serial = 0;
		EQueue Queue = EQueue::Graphics;
		bool bUnavailable = false;

		VkQueryPool Pool = VK_NULL_HANDLE;
		PFN_vkCmdWriteAccelerationStructuresPropertiesKHR Write = nullptr;
	};

	struct DepthResolve final
	{
		VkDevice Device = VK_NULL_HANDLE;

		VkRenderPass Pass = VK_NULL_HANDLE;
		VkFramebuffer Framebuffer = VK_NULL_HANDLE;
		VkImageView Views[2]{};

		PFN_vkDestroyRenderPass DestroyPass = nullptr;
		PFN_vkDestroyFramebuffer DestroyFramebuffer = nullptr;
		PFN_vkDestroyImageView DestroyView = nullptr;
		~DepthResolve();
	};

	struct Pending final
	{
		CommandListStatistics Statistics;

		struct PreparedBinding final
		{
			explicit PreparedBinding(core::MemoryResource& resource) noexcept;

			VkDescriptorSet Sets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
			core::SharedPtr<PreparedDescriptorSet> DescriptorOwners[MAXIMUM_FIXED_BINDING_LAYOUTS];

			core::Array<Buffer*> StorageBuffers;
			core::uint32 BufferCount = 0;

			core::Array<Texture*> StorageTextures;
			core::Array<TextureViewDesc> StorageRanges;
			core::uint32 TextureCount = 0;

			core::uint32 Count = 0;

			VkBuffer Vertices[4]{};
			VkDeviceSize VertexOffsets[4]{};
			core::uint32 VertexCount = 0;

			VkBuffer Index = VK_NULL_HANDLE;
			VkDeviceSize IndexOffsetBytes = 0;
			VkIndexType IndexType = VK_INDEX_TYPE_UINT16;

			core::SharedPtr<PreparedShaderTable> RayTable;
			VkStridedDeviceAddressRegionKHR Rays[4]{};
		};

		struct PreparedAccelBuild final
		{
			explicit PreparedAccelBuild(core::MemoryResource& resource) noexcept;

			VkAccelerationStructureBuildGeometryInfoKHR Build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			core::Array<VkAccelerationStructureGeometryKHR> Geometries;
			core::Array<VkAccelerationStructureBuildRangeInfoKHR> Ranges;
		};

		struct PreparedAccelQuery final
		{
			void* Source = nullptr;
			core::SharedPtr<PreparedAccelQueryOwner> Owner;
		};

		struct PreparedAccelCompact final
		{
			void* Source = nullptr;
			void* Target = nullptr;
			PFN_vkCmdCopyAccelerationStructureKHR Copy = nullptr;
		};

		core::Array<PreparedAccelBuild> PreparedAccelBuilds;
		core::Array<PreparedAccelQuery> PreparedAccelQueries;
		core::Array<PreparedAccelCompact> PreparedAccelCompacts;

		core::Array<core::SharedPtr<NativeBindingProgram>> PreparedBindings;
		[[nodiscard]] const PreparedBinding& BindingAt(core::uint32 index) const noexcept
		{
			return *static_cast<const PreparedBinding*>(PreparedBindings[index]->Data);
		}

		core::Array<core::UniquePtr<DepthResolve>> DepthResolves;

		core::Array<Buffer*> PreparedHostReads;

		core::Array<NativePreparedTextureAccess> PreparedTextures;

		bool bPrepared = false;
		bool bPreparedFinished = false;

		core::uint32 LastPreparedDraw = UINT32_MAX;

		bool bSubmissionCredit = false;

		struct BufferUavBarrierPolicy final
		{
			VkBuffer Resource = VK_NULL_HANDLE;
			bool bEnabled = true;
			bool bFirstBarrierPlaced = false;
		};

		struct TextureUavBarrierPolicy final
		{
			VkImage Resource = VK_NULL_HANDLE;
			bool bEnabled = true;
			bool bFirstBarrierPlaced = false;
		};

		struct DescriptorPoolRecord final
		{
			VkDescriptorPool Handle = VK_NULL_HANDLE;

			core::uint32 UsedSets = 0;

			bool bAccelerationStructures = false;
			bool bDedicated = false;
		};

		struct TextureLayoutState final
		{
			explicit TextureLayoutState(core::MemoryResource& resource) noexcept;

			Texture* Item = nullptr;
			VkImageLayout WholeLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			core::Array<VkImageLayout> SubresourceLayouts;
		};

		explicit Pending(core::MemoryResource& resource) noexcept;

		VkCommandBuffer Commands = VK_NULL_HANDLE;

		VkSemaphore ExternalSemaphore = VK_NULL_HANDLE;
		core::uint64 ExternalSemaphoreValue = 0;
		bool bExternalAcquire = false;

		VkCommandPool Pool = VK_NULL_HANDLE;

		EShadingRate CurrentShadingRate = EShadingRate::Rate1x1;
		EShadingRateCombiner CurrentPrimitiveCombiner{}, CurrentImageCombiner{};
		bool bShadingRateImageActive = false;
		bool bShadingRateSet = false;

		core::Array<Buffer*> Temporary;

		core::Array<TextureLayoutState> TextureLayouts;

		core::Array<VkBufferMemoryBarrier> GraphicsStorageBuffers;
		core::Array<VkImageMemoryBarrier> GraphicsStorageImages;

		core::Array<BufferUavBarrierPolicy> BufferUavPolicies;
		core::Array<TextureUavBarrierPolicy> TextureUavPolicies;

		bool bInvalidTextureLayouts = false;
		bool bMissingManualState = false;
		bool bAutomaticBarriers = true;
		bool bExplicitStateRequest = false;

		core::Array<DescriptorPoolRecord> DescriptorPools;
		core::Array<core::SharedPtr<PreparedDescriptorSet>> DescriptorOwners;

		core::Array<VkFramebuffer> Framebuffers;

		core::Array<core::uint64> AftermathMarkers;

		const GraphicsPipeline* LastTexturePipeline = nullptr;
		void* LastGraphicsAccelStruct = nullptr;

		void* LastTextures[32]{};
		void* LastSamplers[32]{};
		void* LastViews[32]{};
		core::uint32 LastTextureCount = 0;

		void* LastStorageTextures[32]{};
		void* LastStorageViews[32]{};
		core::uint32 LastStorageTextureCount = 0;

		void* LastGraphicsBuffers[32]{};
		BufferRange LastGraphicsBufferRanges[32]{};
		core::uint32 LastGraphicsBufferCount = 0;

		bool bGraphicsStorageWritten = false;

		bool bRangeStorageBarrierPending = false;
		NativeTableRangeBinding RangeStorageTables[MAXIMUM_FIXED_BINDING_LAYOUTS * MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		core::uint32 RangeStorageTableCount = 0;

		VkRenderPassBeginInfo ResumeRendering{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

		VkDescriptorSet LastTextureSet = VK_NULL_HANDLE;
		VkDescriptorSet LastTextureSets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 LastTextureSetCount = 0;

		const ComputePipeline* LastComputePipeline = nullptr;
		void* LastComputeAccelStruct = nullptr;

		void* LastComputeBuffers[32]{};
		BufferRange LastComputeBufferRanges[32]{};
		core::uint32 LastComputeBufferCount = 0;

		void* LastComputeTextures[32]{};
		void* LastComputeSamplers[32]{};
		void* LastComputeViews[32]{};
		core::uint32 LastComputeTextureCount = 0;

		VkDescriptorSet LastComputeSet = VK_NULL_HANDLE;
		VkDescriptorSet LastComputeSets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};

		SwapChain* SwapChainOwner = nullptr;
		core::uint32 SwapChainImageIndex = 0;
		core::uint32 SwapChainSemaphoreIndex = 0;

		EQueue QueueKind = EQueue::Graphics;
		core::uint64 Serial = 0;
	};

	struct NativeAccelStruct;

	struct RetiredAccelStruct final
	{
		NativeAccelStruct* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct NativeMicromap;

	struct RetiredMicromap final
	{
		NativeMicromap* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct Retired final
	{
		Buffer* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct RetiredTexture final
	{
		Texture* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct RetiredPipeline final
	{
		ComputePipeline* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct RetiredGraphicsPipeline final
	{
		GraphicsPipeline* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct RetiredRayPipeline final
	{
		RayTracingPipeline* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct RetiredFramebuffer final
	{
		Framebuffer* Item = nullptr;
		core::uint64 Serial = 0;
	};
}
