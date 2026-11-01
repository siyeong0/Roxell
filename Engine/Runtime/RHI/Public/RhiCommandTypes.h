#pragma once
#include <RhiBindingTypes.h>
#include <RhiFormat.h>
#include <RhiPipelineTypes.h>
#include <RhiResourceTypes.h>
#include <RhiTypes.h>
#include <Span.hpp>
#include <Types.h>
namespace rox::rhi
{
	struct ParallelRecordingStatistics final
	{
		core::uint64 NativeCalls = 0;
		core::uint32 PeakConcurrentNativeCalls = 0;
	};

	struct CommandListStatistics final
	{
		core::uint64 BarrierCalls = 0;
		core::uint64 BarrierCount = 0;

		core::uint64 DescriptorWriteCalls = 0;
		core::uint64 DescriptorCount = 0;

		core::uint64 PreparedShaderTableCreations = 0;
		core::uint64 PreparedShaderTableReuses = 0;

		core::uint64 DescriptorTableMaterializations = 0;
		core::uint64 DescriptorTableCacheHits = 0;

		core::uint64 BindingProgramCreations = 0;
		core::uint64 BindingProgramReuses = 0;

		core::uint64 DescriptorCacheFallbacks = 0;
		core::uint64 SharedSamplerDescriptorReuses = 0;

		core::uint32 DescriptorCacheResourceHighWater = 0;
		core::uint32 DescriptorCacheSamplerHighWater = 0;
	};

	struct BufferCopy final
	{
		BufferHandle Source;
		BufferHandle Destination;

		core::uint64 SourceOffsetBytes = 0;
		core::uint64 DestinationOffsetBytes = 0;

		core::uint64 ByteCount = 0;
	};

	struct TextureCopy final
	{
		TextureHandle Source;
		TextureHandle Destination;
	};

	struct TextureCopyRegion final
	{
		TextureHandle Source;
		TextureHandle Destination;

		TextureSubresource SourceSubresource;
		TextureSubresource DestinationSubresource;

		core::uint32 SourceX = 0;
		core::uint32 SourceY = 0;
		core::uint32 DestinationX = 0;
		core::uint32 DestinationY = 0;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		core::uint32 SourceZ = 0;
		core::uint32 DestinationZ = 0;

		core::uint32 Depth = 1;
	};

	struct TextureBufferCopy final
	{
		BufferHandle Buffer;
		TextureHandle Texture;
		TextureSubresource Subresource;

		core::uint64 BufferOffsetBytes = 0;
	};

	enum class ETextureResolveMode : core::uint8
	{
		None,
		Average,
		SampleZero,
		Minimum,
		Maximum
	};

	struct TextureResolve final
	{
		TextureHandle Source;
		TextureHandle Destination;

		ETextureResolveMode Mode = ETextureResolveMode::Average;
		ETextureResolveMode StencilMode = ETextureResolveMode::None;

		TextureSubresource SourceSubresource;
		TextureSubresource DestinationSubresource;
	};

	enum class EClearColorType : core::uint8
	{
		Float,
		Uint,
		Sint
	};

	class ClearColor final
	{
	public:
		ClearColor() noexcept = default; [[nodiscard]] static ClearColor Float(core::float32 r, core::float32 g, core::float32 b, core::float32 a) noexcept;
		[[nodiscard]] static ClearColor Uint(core::uint32 r, core::uint32 g, core::uint32 b, core::uint32 a) noexcept;
		[[nodiscard]] static ClearColor Sint(core::int32 r, core::int32 g, core::int32 b, core::int32 a) noexcept;
		[[nodiscard]] EClearColorType GetType()const noexcept { return m_Type; }
		[[nodiscard]] const core::float32* GetFloat() const noexcept;
		[[nodiscard]] const core::uint32* GetUint() const noexcept;
		[[nodiscard]] const core::int32* GetSint()const noexcept;

	private:
		ClearColor(core::float32 r, core::float32 g, core::float32 b, core::float32 a) noexcept;

		EClearColorType m_Type = EClearColorType::Float;
		union
		{
			core::float32 m_Float[4]{ 0.0f, 0.0f, 0.0f, 1.0f }; core::uint32 m_Uint[4];
			core::int32 m_Sint[4];
		};
	};

	struct BlendColor final
	{
		core::float32 R = 0.0f;
		core::float32 G = 0.0f;
		core::float32 B = 0.0f;
		core::float32 A = 0.0f;
	};

	struct DepthStencilClear final
	{
		bool bClearDepth = true;
		core::float32 Depth = 1.0f;

		bool bClearStencil = false;
		core::uint8 Stencil = 0;
	};

	enum class EAttachmentLoad : core::uint8
	{
		Load,
		Clear
	};

	struct RenderPassLayout final
	{
		FramebufferHandle Framebuffer;

		core::Span<const EAttachmentLoad> ColorLoads;

		EAttachmentLoad DepthLoad = EAttachmentLoad::Load;
		EAttachmentLoad StencilLoad = EAttachmentLoad::Load;

		bool bReadOnlyDepth = false;
		bool bReadOnlyStencil = false;
	};

	struct RenderPassClearValues final
	{
		core::Span<const ClearColor> Colors;
		core::float32 Depth = 1.0f; core::uint8 Stencil = 0;
	};

	struct RenderPassDesc final
	{
		RenderPassLayout Layout;
		RenderPassClearValues ClearValues;
	};

	struct ViewportDesc final
	{
		core::float32 X = 0.0f;
		core::float32 Y = 0.0f;
		core::float32 Width = 0.0f;
		core::float32 Height = 0.0f;

		core::float32 MinDepth = 0.0f;
		core::float32 MaxDepth = 1.0f;
	};

	struct ScissorDesc final
	{
		core::uint32 X = 0;
		core::uint32 Y = 0;
		core::uint32 Width = 0;
		core::uint32 Height = 0;
	};

	struct ComputeBindings final
	{
		ComputePipelineHandle Pipeline;
		CommandBindings Resources;
	};

	struct RayBindings final
	{
		ShaderTableHandle Table;
		CommandBindings Resources;
	};

	struct GraphicsState final
	{
		GraphicsPipelineHandle Pipeline;

		BufferHandle VertexBuffer;
		BufferHandle IndexBuffer;

		core::uint64 VertexBufferOffsetBytes = 0;
		core::uint64 IndexBufferOffsetBytes = 0;
		EFormat IndexFormat = EFormat::R16_UINT;

		core::Span<const VertexBufferBinding> ExtraVertexBuffers;

		CommandBindings Resources;

		BlendColor BlendConstant;

		EShadingRate ShadingRate = EShadingRate::Rate1x1;
		EShadingRateCombiner PrimitiveShadingRateCombiner = EShadingRateCombiner::Passthrough;
		EShadingRateCombiner ImageShadingRateCombiner = EShadingRateCombiner::Passthrough;
	};

	struct ComputeDispatch final
	{
		core::uint32 GroupsX = 1;
		core::uint32 GroupsY = 1;
		core::uint32 GroupsZ = 1;

		core::Span<const core::Byte> PushConstants;
	};

	struct MeshDispatch final
	{
		core::uint32 GroupsX = 1;
		core::uint32 GroupsY = 1;
		core::uint32 GroupsZ = 1;

		core::Span<const core::Byte> PushConstants;

		core::uint8 StencilReference = 0;
	};

	struct RayDispatch final
	{
		core::uint32 Width = 1;
		core::uint32 Height = 1;
		core::uint32 Depth = 1;

		core::Span<const core::Byte> PushConstants;
	};

	struct DrawArguments final
	{
		core::uint32 VertexCount = 0;
		core::uint32 InstanceCount = 1;
		core::uint32 FirstVertex = 0;
		core::uint32 FirstInstance = 0;

		core::Span<const core::Byte> PushConstants;

		core::uint8 StencilReference = 0;
	};

	struct DrawIndexedArguments final
	{
		core::uint32 IndexCount = 0;
		core::uint32 InstanceCount = 1;
		core::uint32 FirstIndex = 0;
		core::int32 BaseVertex = 0;
		core::uint32 FirstInstance = 0;

		core::Span<const core::Byte> PushConstants;

		core::uint8 StencilReference = 0;
	};

	struct IndirectDispatchArguments final
	{
		core::uint32 GroupsX = 1;
		core::uint32 GroupsY = 1;
		core::uint32 GroupsZ = 1;
	};

	struct ComputeDispatchIndirect final
	{
		ComputePipelineHandle Pipeline;

		BufferHandle Arguments;
		core::uint64 ArgumentsOffsetBytes = 0;

		CommandBindings Bindings;

		core::Span<const core::Byte> PushConstants;
	};

	struct IndirectDrawArguments final
	{
		core::uint32 VertexCount = 0;
		core::uint32 InstanceCount = 1;
		core::uint32 FirstVertex = 0;
		core::uint32 FirstInstance = 0;
	};

	struct IndirectMeshArguments final
	{
		core::uint32 GroupsX = 1;
		core::uint32 GroupsY = 1;
		core::uint32 GroupsZ = 1;
	};

	struct IndirectDrawIndexedArguments final
	{
		core::uint32 IndexCount = 0;
		core::uint32 InstanceCount = 1;
		core::uint32 FirstIndex = 0;
		core::int32 BaseVertex = 0;
		core::uint32 FirstInstance = 0;
	};

	struct GraphicsDrawIndirect final
	{
		GraphicsPipelineHandle Pipeline;

		BufferHandle Arguments;
		core::uint64 ArgumentsOffsetBytes = 0;
		core::uint32 DrawCount = 1;

		BufferHandle VertexBuffer;
		core::uint64 VertexBufferOffsetBytes = 0;

		BufferHandle CountBuffer;
		core::uint64 CountBufferOffsetBytes = 0;

		core::uint8 StencilReference = 0;

		core::Span<const VertexBufferBinding> ExtraVertexBuffers;

		BlendColor BlendConstant;

		EShadingRate ShadingRate = EShadingRate::Rate1x1;
		EShadingRateCombiner PrimitiveShadingRateCombiner = EShadingRateCombiner::Passthrough;
		EShadingRateCombiner ImageShadingRateCombiner = EShadingRateCombiner::Passthrough;

		CommandBindings Bindings;

		core::Span<const core::Byte> PushConstants;
	};

	struct GraphicsDrawIndexedIndirect final
	{
		GraphicsPipelineHandle Pipeline;

		BufferHandle Arguments;
		core::uint64 ArgumentsOffsetBytes = 0;
		core::uint32 DrawCount = 1;

		BufferHandle VertexBuffer;
		BufferHandle IndexBuffer;
		core::uint64 VertexBufferOffsetBytes = 0;
		core::uint64 IndexBufferOffsetBytes = 0;
		EFormat IndexFormat = EFormat::R16_UINT;

		BufferHandle CountBuffer;
		core::uint64 CountBufferOffsetBytes = 0;

		core::uint8 StencilReference = 0;

		core::Span<const VertexBufferBinding> ExtraVertexBuffers;

		BlendColor BlendConstant;

		EShadingRate ShadingRate = EShadingRate::Rate1x1;
		EShadingRateCombiner PrimitiveShadingRateCombiner = EShadingRateCombiner::Passthrough;
		EShadingRateCombiner ImageShadingRateCombiner = EShadingRateCombiner::Passthrough;

		CommandBindings Bindings;

		core::Span<const core::Byte> PushConstants;
	};

	struct MeshDispatchIndirect final
	{
		GraphicsPipelineHandle Pipeline;

		BufferHandle Arguments;
		core::uint64 ArgumentsOffsetBytes = 0;

		core::uint8 StencilReference = 0;

		BlendColor BlendConstant;

		EShadingRate ShadingRate = EShadingRate::Rate1x1;
		EShadingRateCombiner PrimitiveShadingRateCombiner = EShadingRateCombiner::Passthrough;
		EShadingRateCombiner ImageShadingRateCombiner = EShadingRateCombiner::Passthrough;

		CommandBindings Bindings;

		core::Span<const core::Byte> PushConstants;
	};

	static_assert(sizeof(IndirectDispatchArguments) == 12);
	static_assert(sizeof(IndirectDrawArguments) == 16);
	static_assert(sizeof(IndirectMeshArguments) == 12);
	static_assert(sizeof(IndirectDrawIndexedArguments) == 20);

	enum class ESubmitStatus : core::uint8
	{
		Submitted,
		Busy,
		Failed
	};

	struct SubmitOutcome final
	{
		ESubmitStatus Status = ESubmitStatus::Failed;
		SubmissionToken Token;
	};
}
