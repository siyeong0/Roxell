#pragma once

#include "PreparedBindingCache.h"
#include "D3D12/RayLocalRootRecipe.h"
#include "RecordedResourceUses.h"

#include <Array.hpp>
#include <NumericLimits.hpp>
#include <SharedPtr.hpp>

namespace rox::rhi
{
	// Backend pipeline creation consumes resolved bytecode, independently of public shader ownership.
	struct NativeComputePipelineDesc final
	{
		explicit NativeComputePipelineDesc(const ComputePipelineDesc& source) noexcept
			: Compute(source.Compute)
			, PushConstantBytes(source.PushConstantBytes)
			, Layouts(source.Layouts)
			, bHeapDirectlyIndexed(source.bHeapDirectlyIndexed)
			, bDeviceHeapIndexed(source.bDeviceHeapIndexed)
		{
		}

		ShaderStageDesc Compute;

		core::Span<const ComputeBindingDesc> Bindings;

		core::uint32 PushConstantBytes = 0;

		core::uint32 BindlessSampledTextureCapacity = 0;
		core::uint32 BindlessSamplerCapacity = 0;
		core::uint32 BindlessStorageBufferCapacity = 0;
		core::uint32 BindlessStorageTextureCapacity = 0;
		core::uint32 BindlessUniformBufferCapacity = 0;

		core::uint32 AccelStructSlot = 0xFFFFFFFFu;

		core::Span<const BindingLayoutHandle> Layouts;

		bool bHeapDirectlyIndexed = false;
		bool bDeviceHeapIndexed = false;

		EFormat BindlessStorageBufferFormat = EFormat::UNKNOWN;

		core::uint32 BindlessReadOnlyBufferCapacity = 0;
		EFormat BindlessReadOnlyBufferFormat = EFormat::UNKNOWN;

		core::Span<const core::Byte> ShaderBytecode;
		const char* EntryPoint = "main";

		core::Span<const ShaderSpecialization> Specializations;

		ShaderHandle Shader;
	};

	struct NativeGraphicsPipelineDesc final
	{
		explicit NativeGraphicsPipelineDesc(const GraphicsPipelineDesc& source) noexcept
			: ColorFormat(source.ColorFormat)
			, VertexStrideBytes(source.VertexStrideBytes)
			, VertexAttributes(source.VertexAttributes)
			, PushConstantBytes(source.PushConstantBytes)
			, DepthFormat(source.DepthFormat)
			, bDepthTest(source.bDepthTest)
			, bDepthWrite(source.bDepthWrite)
			, ColorFormats(source.ColorFormats)
			, Topology(source.Topology)
			, Raster(source.Raster)
			, DepthCompare(source.DepthCompare)
			, ColorBlends(source.ColorBlends)
			, Layouts(source.Layouts)
			, SampleCount(source.SampleCount)
			, PatchControlPoints(source.PatchControlPoints)
			, Stencil(source.Stencil)
			, ExtraVertexBufferSlots(source.ExtraVertexBufferSlots)
			, ViewportCount(source.ViewportCount)
			, bSupportsShadingRateImage(source.bSupportsShadingRateImage)
			, bHeapDirectlyIndexed(source.bHeapDirectlyIndexed)
			, bDeviceHeapIndexed(source.bDeviceHeapIndexed)
			, InputLayout(source.InputLayout)
			, DepthBounds(source.DepthBounds)
			, Vertex(source.Vertex)
			, Pixel(source.Pixel)
			, Geometry(source.Geometry)
			, Hull(source.Hull)
			, Domain(source.Domain)
			, Mesh(source.Mesh)
			, Task(source.Task)
		{
		}

		EFormat ColorFormat = EFormat::RGBA8_UNORM;

		core::uint32 VertexStrideBytes = 0;
		core::Span<const VertexAttributeDesc> VertexAttributes;

		core::uint32 PushConstantBytes = 0;

		core::Span<const GraphicsTextureBindingDesc> SampledTextures;

		EFormat DepthFormat = EFormat::UNKNOWN;
		bool bDepthTest = false;
		bool bDepthWrite = false;

		core::Span<const EFormat> ColorFormats;

		EPrimitiveTopology Topology = EPrimitiveTopology::TriangleList;
		RasterStateDesc Raster;

		ECompareOp DepthCompare = ECompareOp::Less;

		core::Span<const ColorBlendDesc> ColorBlends;

		core::Span<const BindingLayoutHandle> Layouts;

		core::uint32 SampleCount = 1;

		core::uint32 PatchControlPoints = 0;

		StencilStateDesc Stencil;

		core::uint32 BindlessSampledTextureCapacity = 0;
		core::uint32 BindlessSamplerCapacity = 0;
		core::uint32 BindlessStorageBufferCapacity = 0;
		core::uint32 BindlessStorageTextureCapacity = 0;
		core::uint32 BindlessUniformBufferCapacity = 0;

		core::Span<const GraphicsStorageTextureBindingDesc> StorageTextures;

		core::Span<const VertexBufferSlotDesc> ExtraVertexBufferSlots;

		core::uint32 ViewportCount = 1;

		bool bSupportsShadingRateImage = false;

		core::uint32 AccelStructSlot = 0xFFFFFFFFu;

		bool bHeapDirectlyIndexed = false;
		bool bDeviceHeapIndexed = false;

		InputLayoutHandle InputLayout;

		EFormat BindlessStorageBufferFormat = EFormat::UNKNOWN;

		core::uint32 BindlessReadOnlyBufferCapacity = 0;
		EFormat BindlessReadOnlyBufferFormat = EFormat::UNKNOWN;

		DepthBoundsDesc DepthBounds;

		ShaderStageDesc Vertex;
		ShaderStageDesc Pixel;
		ShaderStageDesc Geometry;
		ShaderStageDesc Hull;
		ShaderStageDesc Domain;
		ShaderStageDesc Mesh;
		ShaderStageDesc Task;

		core::Span<const core::Byte> VertexShaderBytecode;
		core::Span<const core::Byte> FragmentShaderBytecode;
		core::Span<const core::Byte> GeometryShaderBytecode;
		core::Span<const core::Byte> HullShaderBytecode;
		core::Span<const core::Byte> DomainShaderBytecode;
		core::Span<const core::Byte> MeshShaderBytecode;
		core::Span<const core::Byte> TaskShaderBytecode;

		const char* VertexEntryPoint = "main";
		const char* FragmentEntryPoint = "main";
		const char* GeometryEntryPoint = "main";
		const char* HullEntryPoint = "main";
		const char* DomainEntryPoint = "main";
		const char* MeshEntryPoint = "main";
		const char* TaskEntryPoint = "main";

		core::Span<const ShaderSpecialization> VertexSpecializations;
		core::Span<const ShaderSpecialization> FragmentSpecializations;
		core::Span<const ShaderSpecialization> GeometrySpecializations;
		core::Span<const ShaderSpecialization> HullSpecializations;
		core::Span<const ShaderSpecialization> DomainSpecializations;
		core::Span<const ShaderSpecialization> MeshSpecializations;
		core::Span<const ShaderSpecialization> TaskSpecializations;

		ShaderHandle VertexShader;
		ShaderHandle FragmentShader;
		ShaderHandle GeometryShader;
		ShaderHandle HullShader;
		ShaderHandle DomainShader;
		ShaderHandle MeshShader;
		ShaderHandle TaskShader;
	};

	struct PipelineSlot final
	{
		explicit PipelineSlot(core::MemoryResource& resource) noexcept;

		struct FixedLayout final
		{
			BindingLayoutHandle Handle;

			ComputeBindingDesc Buffers[8]{};
			core::uint32 BufferCount = 0;

			ComputeTextureBindingDesc Textures[8]{};
			core::uint32 TextureCount = 0;
			core::uint32 SampledTextureCount = 0;
			core::uint32 StorageTextureCount = 0;
		};

		FixedLayout Layouts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 LayoutCount = 0;

		core::Array<ComputeBindingDesc> Bindings;

		ComputeTextureBindingDesc Textures[8]{};
		core::uint32 TextureCount = 0;
		core::uint32 SampledTextureCount = 0;
		core::uint32 StorageTextureCount = 0;

		core::uint32 PushConstantBytes = 0;

		core::uint32 BindlessSampledTextureCapacity = 0;
		core::uint32 BindlessSamplerCapacity = 0;

		core::uint32 BindlessStorageBufferCapacity = 0;
		EFormat BindlessStorageBufferFormat = EFormat::UNKNOWN;

		core::uint32 BindlessReadOnlyBufferCapacity = 0;
		EFormat BindlessReadOnlyBufferFormat = EFormat::UNKNOWN;

		core::uint32 BindlessStorageTextureCapacity = 0;
		core::uint32 BindlessUniformBufferCapacity = 0;

		bool bHeapDirectlyIndexed = false;
		bool bDeviceHeapIndexed = false;

		core::uint32 AccelStructSlot = 0xFFFFFFFFu;

		BindingLayoutHandle Layout;

		void* Native = nullptr;

		core::usize RecordedUses = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct InputLayoutSlot final
	{
		VertexAttributeDesc Attributes[16]{};
		VertexBufferSlotDesc ExtraVertexBufferSlots[3]{};

		core::uint32 VertexStrideBytes = 0;
		core::uint32 AttributeCount = 0;
		core::uint32 ExtraVertexBufferCount = 0;

		core::uint32 References = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct GraphicsPipelineSlot final
	{
		void* Native = nullptr;

		InputLayoutHandle InputLayout;
		BindingLayoutHandle Layout;

		BindingLayoutHandle Layouts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 LayoutCount = 0;

		EFormat ColorFormat = EFormat::UNKNOWN;
		EFormat ColorFormats[8]{};
		core::uint32 ColorCount = 0;

		EFormat DepthFormat = EFormat::UNKNOWN;
		bool bWritesDepthStencil = false;
		bool bWritesDepth = false;
		bool bWritesStencil = false;

		core::uint32 VertexStrideBytes = 0;
		VertexAttributeDesc VertexAttributes[16]{};
		core::uint32 VertexAttributeCount = 0;

		VertexBufferSlotDesc ExtraVertexBufferSlots[3]{};
		core::uint32 ExtraVertexBufferCount = 0;

		core::uint32 ViewportCount = 1;

		core::uint32 PushConstantBytes = 0;
		core::uint32 AccelStructSlot = 0xFFFFFFFFu;

		core::uint32 SampledTextureCount = 0;

		core::uint32 BindlessSampledTextureCapacity = 0;
		core::uint32 BindlessSamplerCapacity = 0;

		SamplerDesc Samplers[8]{};

		core::uint32 StorageTextureCount = 0;

		core::uint32 BindlessStorageBufferCapacity = 0;
		EFormat BindlessStorageBufferFormat = EFormat::UNKNOWN;

		core::uint32 BindlessReadOnlyBufferCapacity = 0;
		EFormat BindlessReadOnlyBufferFormat = EFormat::UNKNOWN;

		core::uint32 BindlessStorageTextureCapacity = 0;
		core::uint32 BindlessUniformBufferCapacity = 0;

		core::uint32 SampleCount = 1;

		bool bSupportsShadingRateImage = false;

		bool bMeshShader = false;
		bool bTaskShader = false;

		bool bHeapDirectlyIndexed = false;
		bool bDeviceHeapIndexed = false;

		GraphicsBufferBindingDesc BufferBindings[32]{};
		core::uint32 BufferBindingCount = 0;

		core::usize RecordedUses = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct RayTracingPipelineSlot final
	{
		explicit RayTracingPipelineSlot(core::MemoryResource& resource) noexcept;

		core::Array<RayLocalRootRecipe> LocalRootRecipes;
		bool bTypedLocalRoots = false;

		void* Native = nullptr;

		struct GlobalLayout final
		{
			BindingLayoutHandle Handle;

			ComputeBindingDesc GlobalBuffers[8]{};
			core::uint32 GlobalBufferCount = 0;

			ComputeTextureBindingDesc Textures[8]{};
			core::uint32 TextureCount = 0;
			core::uint32 SampledTextureCount = 0;
			core::uint32 StorageTextureCount = 0;
		};

		GlobalLayout Layouts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 LayoutCount = 0;

		BindingLayoutHandle GeneralLocalLayouts[32]{};
		BindingLayoutHandle HitLocalLayouts[32]{};

		core::uint32 GeneralLocalDataBytes[32]{};
		core::uint32 HitLocalDataBytes[32]{};

		core::uint32 AccelStructSlot = 0xFFFFFFFFu;
		core::uint32 PushConstantBytes = 0;

		EShaderStage GeneralStages[32]{};
		core::uint32 GeneralShaderCount = 0;
		core::uint32 HitGroupCount = 0;

		core::uint32 ShaderTableReferences = 0;
		core::uint32 PreparedReferences = 0;
		core::usize RecordedUses = 0;

		core::uint32 Generation = 1;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
		bool bLive = false;
		bool bExhausted = false;
	};

	class PreparedShaderTable final
	{
	public:
		PreparedShaderTable(SelectedBackendState* backend, void* native) noexcept;
		~PreparedShaderTable() noexcept;
		PreparedShaderTable(const PreparedShaderTable&) = delete;
		PreparedShaderTable& operator=(const PreparedShaderTable&) = delete;

		[[nodiscard]] void* GetNative() const noexcept { return m_Native; }

	private:
		SelectedBackendState* m_Backend;
		void* m_Native;
	};

	struct ShaderTableSlot final
	{
		explicit ShaderTableSlot(core::MemoryResource& resource) noexcept;

		core::SharedPtr<PreparedShaderTable> PreparedTable;

		core::Array<core::Byte> LocalData;
		core::Array<BindingSetHandle> TypedLocalSets;
		core::uint32 TypedLocalSetOffsets[98]{};

		core::uint32 PreparedReferences = 0;

		core::uint32 LocalDataOffsets[98]{};

		RayTracingPipelineHandle Pipeline;

		core::uint32 RayGenerationShaderIndex = 0;
		core::uint32 MissShaderIndices[32]{};
		core::uint32 HitGroupIndices[32]{};
		core::uint32 CallableShaderIndices[32]{};

		BindingSetHandle RayGenerationLocalSet;
		BindingSetHandle MissLocalSets[32]{};
		BindingSetHandle HitGroupLocalSets[32]{};
		BindingSetHandle CallableLocalSets[32]{};

		core::uint32 MissShaderCount = 0;
		core::uint32 HitGroupCount = 0;
		core::uint32 CallableShaderCount = 0;

		core::uint32 Generation = 1;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
		bool bLive = false;
		bool bExhausted = false;
	};

	struct BindingLayoutSlot final
	{
		[[nodiscard]] ComputeBindingDesc ComputeBuffer(core::uint32 index) const noexcept;
		[[nodiscard]] ComputeTextureBindingDesc ComputeTexture(core::uint32 index) const noexcept;
		[[nodiscard]] GraphicsBufferBindingDesc GraphicsBuffer(core::uint32 index) const noexcept;
		[[nodiscard]] GraphicsTextureBindingDesc GraphicsTexture(core::uint32 index) const noexcept;
		[[nodiscard]] GraphicsStorageTextureBindingDesc GraphicsStorageTexture(core::uint32 index) const noexcept;

		BindingLayoutEntry Entries[16]{};
		core::uint8 BufferEntries[8]{};
		core::uint8 TextureEntries[8]{};
		core::uint8 StorageTextureEntries[8]{};
		[[nodiscard]] BindingRangeDesc Range(core::uint32 index) const noexcept;

		core::uint32 RangeCount = 0;
		core::uint32 Space = 0;
		core::uint32 RangeSamplerCount = 0;
		core::uint32 RangeAccelStructCount = 0;

		EBindingLayoutKind Kind = EBindingLayoutKind::Compute;

		core::uint32 ComputeCount = 0;
		core::uint32 ComputeTextureCount = 0;
		core::uint32 ComputeSampledCount = 0;
		core::uint32 ComputeStorageCount = 0;

		core::uint32 GraphicsCount = 0;
		core::uint32 GraphicsStorageCount = 0;
		core::uint32 GraphicsBufferCount = 0;

		core::uint32 References = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct BindingLayoutScratch final
	{
		void Capture(const BindingLayoutSlot& layout) noexcept;
		ComputeBindingDesc ComputeBuffers[8]{};
		ComputeTextureBindingDesc ComputeTextures[8]{};
		GraphicsTextureBindingDesc GraphicsTextures[8]{};
		GraphicsStorageTextureBindingDesc GraphicsStorageTextures[8]{};
		GraphicsBufferBindingDesc GraphicsBuffers[8]{};
	};

	struct BindingSetSlot final
	{
		core::SharedPtr<BoundDescriptorTables> Tables;
		core::SharedPtr<PreparedBindingCache> Cache;

		BindingLayoutHandle Layout;

		TextureHandle SampledTextures[8]{};
		TextureViewHandle SampledViews[8]{};
		SamplerHandle Samplers[8]{};

		TextureHandle StorageTextures[8]{};
		TextureViewHandle StorageViews[8]{};

		BufferHandle Buffers[8]{};
		BufferRange BufferRanges[8]{};
		BufferViewHandle BufferViews[8]{};

		core::uint32 TextureCount = 0;
		core::uint32 SamplerCount = 0;
		core::uint32 StorageTextureCount = 0;
		core::uint32 BufferCount = 0;

		AccelStructHandle AccelStructs[8]{};
		core::uint32 AccelStructCount = 0;

		core::uint32 ShaderTableReferences = 0;
		core::uint32 PreparedReferences = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct DescriptorTableEntry final
	{
		TextureHandle Texture;
		TextureViewHandle View;
		SamplerHandle Sampler;

		BufferHandle Buffer;
		BufferRange Range;
		BufferViewHandle BufferView;

		AccelStructHandle AccelerationStructure;
		core::SharedPtr<core::Array<AccelStructHandle>> BottomReferences;
	};

	struct DescriptorTableVersion
	{
		explicit DescriptorTableVersion(core::MemoryResource& resource) noexcept;

		core::Array<DescriptorTableEntry> Entries;

		void* Native = nullptr;

		core::uint64 LastUse = 0;
		core::usize RecordedUses = 0;
	};

	struct DescriptorTableSlot final : DescriptorTableVersion
	{
		core::uint32 BoundReferences = 0;

		explicit DescriptorTableSlot(core::MemoryResource& resource) noexcept;

		core::Array<DescriptorTableVersion> RetiredVersions;

		DescriptorTableDesc Descriptor;

		core::uint32 Generation = 1;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
		bool bLive = false;
		bool bExhausted = false;
	};
}
