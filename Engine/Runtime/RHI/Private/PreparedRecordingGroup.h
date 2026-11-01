#pragma once

#include "AcquiredImagePlan.h"
#include "PreparedBindingCache.h"
#include "PreparedProgramCache.h"
#include "DeviceResources.h"
#include "DevicePipelines.h"

#include <Array.hpp>
#include <HashMap.hpp>
#include <MemoryResource.h>
#include <RhiCommandPreparation.h>
#include <RhiCommandTypes.h>
#include <RhiFormat.h>
#include <RhiRayTracingTypes.h>
#include <RhiResourceTypes.h>
#include <RhiTypes.h>
#include <ThreadPrimitives.hpp>

namespace rox::rhi
{
	struct PreparedRecordingAttachment final
	{
		FramebufferHandle Framebuffer;

		EAttachmentLoad ColorLoads[8]{};
		core::uint32 ColorCount = 0;

		EDepthLoadOp DepthLoad = EDepthLoadOp::Load;
		bool bReadOnlyDepth = false;

		EDepthLoadOp StencilLoad = EDepthLoadOp::Load;
		bool bReadOnlyStencil = false;
	};

	struct PreparedRecordingTextureBufferTransfer final
	{
		PreparedTextureBufferCopyDesc Description;
		NativeTextureBufferCopy Native;
	};

	struct PreparedRecordingResolve final
	{
		TextureResolve Description;
		NativeTextureResolve Native;
	};

	struct PreparedRecordingIndirect final
	{
		PreparedIndirectDesc Description;
		NativePreparedIndirect Native;
	};

	struct PreparedRecordingAccelBuild final
	{
		explicit PreparedRecordingAccelBuild(core::MemoryResource& memory) noexcept;

		PreparedAccelStructBuildDesc Description;

		core::Array<AccelTriangleGeometryDesc> Triangles;
		core::Array<AccelAabbGeometryDesc> Aabbs;
		core::Array<AccelStructHandle> ReferencedBlas;

		core::Array<NativeAccelTriangleGeometry> NativeTriangles;
		core::Array<NativeAccelAabbGeometry> NativeAabbs;

		core::Array<core::uint64> Shape;
		core::uint64 BuildRevision = 0;

		core::uint32 NativeIndex = 0;
		bool bEmitted = false;
	};

	struct PreparedRecordingAccelQuery final
	{
		PreparedAccelStructCompactionQueryDesc Description;

		core::uint64 QueryGeneration = 0;
		core::uint64 BuildRevision = 0;

		core::uint32 NativeIndex = 0;
		bool bEmitted = false;
	};

	struct PreparedRecordingAccelCompact final
	{
		PreparedAccelStructCompactDesc Description;

		core::uint64 BuildRevision = 0;

		core::uint32 NativeIndex = 0;
		bool bEmitted = false;
	};

	struct PreparedRecordingPlannedAccel final
	{
		const PreparedRecordingAccelBuild* Build = nullptr;
		AccelStructHandle ShapeOwner;

		core::uint32 PinCount = 0;
		core::uint32 ParentPinCount = 0;

		core::uint64 BuildRevision = 0;

		bool bAvailable = false;
		bool bAllowUpdate = false;
		bool bAllowCompaction = false;
		bool bQueryPlanned = false;
	};

	struct PreparedRecordingPhase final
	{
		using BufferLookupEntry = PreparedAccessProgram::BufferLookupEntry;
		explicit PreparedRecordingPhase(core::MemoryResource& memory, core::SharedPtr<PreparedAccessProgram> program) noexcept;
		core::SharedPtr<PreparedAccessProgram> AccessProgram;

		[[nodiscard]] bool BuildAccessLookup();
		void CompleteBufferLookup() noexcept;

		[[nodiscard]] bool HasBufferStateConflict(BufferHandle buffer) const noexcept;
		[[nodiscard]] bool ContainsBuffer(BufferHandle buffer, EBufferState state, core::uint64 offset, core::uint64 bytes) const noexcept;
		[[nodiscard]] core::Span<const core::uint32> FindTextureAccesses(TextureHandle texture) const noexcept;

		core::Array<PreparedBufferAccessDesc>& Buffers;
		core::Array<PreparedTextureAccessDesc>& Textures;

		core::Array<BufferLookupEntry>& BufferLookup;
		core::Array<core::uint32>& TextureLookup;
		bool bBufferLookupReady = false;

		core::Array<PreparedBinding> Bindings;
		// Exact fixed-set coverage is reusable only inside this immutable phase.
		core::HashMap<core::uint64, BindingSetHandle>& ValidatedFixedSets;

		core::Array<PreparedRecordingAttachment> Attachments;

		core::Array<PreparedRecordingTextureBufferTransfer> TextureBufferCopies;
		core::Array<PreparedRecordingResolve> Resolves;

		core::Array<PreparedRecordingIndirect> Indirects;

		core::Array<PreparedAccelStructAccessDesc> AccelStructs;
		core::Array<PreparedRecordingAccelBuild> AccelBuilds;
		core::Array<PreparedRecordingAccelQuery> AccelCompactionQueries;
		core::Array<PreparedRecordingAccelCompact> AccelCompactions;

		core::Array<NativePreparedBufferAccess>& NativeBuffers;
		core::Array<NativePreparedTextureAccess>& NativeTextures;

		PreparedCommandCapacity Capacity;
		PreparedCommandCapacity Used;

		GpuTimerHandle GpuTimer;
	};

	struct PreparedRecordingList final
	{
		explicit PreparedRecordingList(core::MemoryResource& memory) noexcept;

		CommandListHandle Handle;
		EQueue Queue = EQueue::Graphics;

		core::uint32 PredecessorOffset = 0;
		core::uint32 PredecessorCount = 0;
		core::uint32 DependencyFrontier[3]{};

		SubmissionToken AcceptedToken;
		ESubmissionDisposition Disposition = ESubmissionDisposition::NotAccepted;

		core::Array<PreparedRecordingPhase> Phases;
		core::Array<NativePreparedPhase> NativePhases;

		core::uint32 ActivePhase = 0xffffffffu;
		core::uint32 ActiveAttachment = 0xffffffffu;

		core::EErrorCode FailureCode = core::EErrorCode::None;
		core::uint32 Diagnostic = 0;

		bool bFinished = false;
		bool bConsumed = false;
	};

	class PreparedRecordingGroup final
	{
	public:

		explicit PreparedRecordingGroup(core::MemoryResource& memory) noexcept;

		[[nodiscard]] core::Result<void> Initialize(const CommandListGroupPreparationDesc& description, PreparedProgramCache& cache);
		void BuildDependencyFrontier(core::uint32 listIndex) noexcept;
		[[nodiscard]] bool HappensBefore(core::uint32 producer, core::uint32 consumer) const noexcept;

		core::Array<PreparedRecordingList> Lists;
		core::SharedPtr<PreparedGroupTopology> Topology;
		core::Span<const core::uint32> Predecessors;

		core::Array<PreparedRecordingPlannedAccel> PlannedAccels;

		AcquiredImagePlan AcquiredImages;

		EQueue Queue = EQueue::Graphics;
		EPreparedQueuePolicy QueuePolicy = EPreparedQueuePolicy::SingleQueue;

		EPreparedRecordingMode Mode = EPreparedRecordingMode::Serial;
		core::ExecutionThreadId RecordingThread;

		core::uint32 NextSubmit = 0;
		bool bRecording = false;
		bool bRecorded = false;
		bool bSubmissionFailed = false;

	private:
		core::uint32 m_LastQueueList[3]{ 0xffffffffu, 0xffffffffu, 0xffffffffu };
	};
}
