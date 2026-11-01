#pragma once

#include <ComparisonTypes.hpp>
#include <Result.hpp>
#include <RhiBindingTypes.h>
#include <RhiCommandTypes.h>
#include <RhiFormat.h>
#include <RhiInterop.h>
#include <RhiRayTracingTypes.h>
#include <RhiResourceTypes.h>
#include <RhiTypes.h>
#include <Span.hpp>
#include <Types.h>
#include <Vocabulary.hpp>

namespace rox::rhi
{
	// The native command-list borrow exists only during this immediate prepared callback.
	using NativeCommandCallback = core::EErrorCode(*)(void* userDataOrNull, NativeCommandListHandle commands) noexcept;

	enum class EPreparedRecordingMode : core::uint8
	{
		Serial,
		Parallel
	};

	struct PreparedBufferAccessDesc final
	{
		BufferHandle Buffer;
		BufferRange Range;
		EBufferState State = EBufferState::CopySource;
	};

	struct PreparedTextureAccessDesc final
	{
		TextureHandle Texture;

		core::uint32 BaseMipLevel = 0;
		core::uint32 MipLevelCount = 1;

		core::uint32 BaseArrayLayer = 0;
		core::uint32 ArrayLayerCount = 1;

		ETextureAspect Aspect = ETextureAspect::Default;

		ETextureState State = ETextureState::ShaderRead;
	};

	struct PreparedVertexBufferBinding final
	{
		BufferHandle Buffer;
		BufferRange Range;
	};

	struct PreparedBindingDesc final
	{
		[[nodiscard]] static PreparedBindingDesc Compute(ComputePipelineHandle pipeline) noexcept;
		[[nodiscard]] static PreparedBindingDesc Graphics(GraphicsPipelineHandle pipeline) noexcept;
		[[nodiscard]] static PreparedBindingDesc Ray(ShaderTableHandle table) noexcept;

		core::Variant<ComputePipelineHandle, GraphicsPipelineHandle, ShaderTableHandle> Pipeline;

		core::Span<const BindingSetHandle> BindingSets;

		// Implicit consecutive vertex slots; ranges and index format are fixed during preparation.
		core::Span<const PreparedVertexBufferBinding> VertexBuffers;

		BufferHandle IndexBuffer;
		BufferRange IndexRange;
		EFormat IndexFormat = EFormat::R16_UINT;

		core::Span<const DescriptorHeapBinding> DeviceHeaps;
	};

	enum class ETextureBufferCopyDirection : core::uint8
	{
		BufferToTexture,
		TextureToBuffer
	};

	struct PreparedTextureBufferCopyDesc final
	{
		TextureBufferCopy Copy;
		ETextureBufferCopyDirection Direction = ETextureBufferCopyDirection::BufferToTexture;
	};

	struct PreparedCommandCapacity final
	{
		core::uint32 BufferCopies = 0;
		core::uint32 TextureCopies = 0;

		core::uint32 Dispatches = 0;
		core::uint32 Draws = 0;

		core::uint32 RenderPasses = 0;
		core::uint32 ViewportChanges = 0;
		core::uint32 ScissorChanges = 0;

		core::uint32 MemoryBarriers = 0;

		core::uint32 RayDispatches = 0;

		core::uint32 AccelBuilds = 0;
		core::uint32 AccelCompactionQueries = 0;
		core::uint32 AccelCompactions = 0;

		core::uint32 NativeCallbacks = 0;
	};

	enum class EPreparedIndirectKind : core::uint8
	{
		Compute,
		Draw,
		DrawIndexed,
		Mesh
	};

	struct PreparedIndirectDesc final
	{
		EPreparedIndirectKind Kind = EPreparedIndirectKind::Compute;
		core::uint32 BindingSlot = 0xffffffffu;

		BufferHandle Arguments;
		BufferRange ArgumentsRange;
		// Zero selects the packed native record size; other strides are multiples of four.
		core::uint32 ArgumentStrideBytes = 0;
		core::uint32 MaximumCount = 1;

		BufferHandle CountBuffer;
		core::uint64 CountBufferOffsetBytes = 0;
	};

	struct PreparedIndirectRef final
	{
		CommandListHandle List;
		core::uint32 Phase = 0xffffffffu;
		core::uint32 Slot = 0xffffffffu;
	};

	struct PreparedAccelStructAccessDesc final
	{
		AccelStructHandle AccelStruct;
		EAccelStructState State = EAccelStructState::ShaderRead;
	};

	struct PreparedAccelStructBuildDesc final
	{
		AccelStructHandle Target;
		AccelStructBuildDesc Build;

		BufferHandle Scratch;
		core::uint64 ScratchOffsetBytes = 0;

		// Top-level instances use the existing 64-byte AccelGpuInstanceDesc representation.
		BufferHandle Instances;
		core::uint64 InstanceOffsetBytes = 0;

		// Exhaustive caller ownership envelope for BLAS addresses used by GPU instance records.
		core::Span<const AccelStructHandle> ReferencedBlas;
	};

	struct PreparedAccelStructCompactionQueryDesc final
	{
		AccelStructHandle Source;
	};

	struct PreparedAccelStructCompactDesc final
	{
		AccelStructHandle Source;
		AccelStructHandle Target;
	};

	struct PreparedAccelStructBuildRef final
	{
		CommandListHandle List;
		core::uint32 Phase = 0xffffffffu;
		core::uint32 Slot = 0xffffffffu;
	};

	struct PreparedAccelStructCompactionQueryRef final
	{
		CommandListHandle List;
		core::uint32 Phase = 0xffffffffu;
		core::uint32 Slot = 0xffffffffu;
	};

	struct PreparedAccelStructCompactRef final
	{
		CommandListHandle List;
		core::uint32 Phase = 0xffffffffu;
		core::uint32 Slot = 0xffffffffu;
	};

	struct CommandAccessPhaseDesc final
	{
		core::Span<const PreparedBufferAccessDesc> Buffers;
		core::Span<const PreparedTextureAccessDesc> Textures;

		core::Span<const PreparedBindingDesc> Bindings;

		core::Span<const RenderPassLayout> Attachments;

		PreparedCommandCapacity Capacity;

		core::Span<const PreparedTextureBufferCopyDesc> TextureBufferCopies;
		core::Span<const TextureResolve> Resolves;

		core::Span<const PreparedIndirectDesc> Indirects;

		core::Span<const PreparedAccelStructAccessDesc> AccelStructs;
		core::Span<const PreparedAccelStructBuildDesc> AccelBuilds;
		core::Span<const PreparedAccelStructCompactionQueryDesc> AccelCompactionQueries;
		core::Span<const PreparedAccelStructCompactDesc> AccelCompactions;

		// Optional idle timer, exclusively retained until discard or native retirement.
		GpuTimerHandle GpuTimer;
	};

	struct CommandListPreparationDesc final
	{
		CommandListHandle List;
		core::Span<const CommandAccessPhaseDesc> Phases;
	};

	struct PreparedResolveRef final
	{
		CommandListHandle List;
		core::uint32 Phase = 0xffffffffu;
		core::uint32 Slot = 0xffffffffu;
	};

	enum class EPreparedQueuePolicy : core::uint8
	{
		SingleQueue,
		MultiQueue
	};

	struct PreparedCommandListDependency final
	{
		core::uint32 Producer = 0;
		core::uint32 Consumer = 0;
	};

	struct PreparedExecutionStatistics final
	{
		core::uint64 AccessProgramBuilds = 0;
		core::uint64 AccessProgramReuses = 0;
	};
	struct CommandListGroupPreparationDesc final
	{
		core::Span<const CommandListPreparationDesc> Lists;
		core::Span<const PreparedCommandListDependency> Dependencies;

		EPreparedQueuePolicy QueuePolicy = EPreparedQueuePolicy::SingleQueue;

		PreparedExecutionStatistics* Statistics = nullptr;
	};

	struct PreparedTextureBufferCopyRef final
	{
		CommandListHandle List;
		core::uint32 Phase = 0xffffffffu;
		core::uint32 Slot = 0xffffffffu;
	};

	struct PreparedBindingRef final
	{
		CommandListHandle List;
		core::uint32 Phase = 0;
		core::uint32 Slot = 0;
		auto operator<=>(const PreparedBindingRef&) const = default;
	};

	struct PreparedAttachmentRef final
	{
		CommandListHandle List;
		core::uint32 Phase = 0;
		core::uint32 Slot = 0;
		auto operator<=>(const PreparedAttachmentRef&) const = default;
	};

	struct PreparedIndexedDraw final
	{
		PreparedBindingRef Binding;
		DrawIndexedArguments Draw;
	};
} // namespace rox::rhi
