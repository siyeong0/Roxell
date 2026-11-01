#pragma once

#include "DeviceCommands.h"

namespace rox::rhi
{
	struct RecordedCopy final
	{
		BufferHandle SourceHandle;
		BufferHandle DestinationHandle;
		NativeCopy Native;
	};

	struct RecordedTextureCopy final
	{
		TextureHandle SourceHandle;
		TextureHandle DestinationHandle;
		NativeTextureCopy Native;
	};

	struct RecordedTextureUse final
	{
		TextureHandle Handle;
		void* Native = nullptr;

		bool bTrackingSeed = false;

		ETrackedResourceState State = ETrackedResourceState::Unknown;
		bool bPermanentTransition = false;

		bool bSharedRead = false;
		bool bPreparedForSharedRead = false;
	};

	struct RecordedBufferUse final
	{
		BufferHandle Handle;
		void* Native = nullptr;

		ETrackedResourceState State = ETrackedResourceState::Unknown;

		bool bTrackingSeed = false;
		bool bPermanentTransition = false;

		bool bExplicitStateRequest = false;
		bool bAutomaticBarriers = true;
	};

	struct RecordedAccelUse final
	{
		AccelStructHandle Handle;
		void* Native = nullptr;
		bool bBuildWriteState = false;
	};

	struct AccelBuildSignature final
	{
		core::uint64 First = 0;
		core::uint64 Second = 0;
		core::StrongOrdering operator<=>(const AccelBuildSignature&) const = default;
	};

	struct RecordedAccelBuild final
	{
		AccelStructHandle Handle;
		AccelBuildSignature Signature;

		bool bAllowUpdate = false;
		bool bAllowCompaction = false;
	};

	struct RecordedPipelineUse final
	{
		ComputePipelineHandle Handle;
		void* Native = nullptr;
	};

	struct RecordedGraphicsPipelineUse final
	{
		GraphicsPipelineHandle Handle;
		void* Native = nullptr;
	};

	struct RecordedRayPipelineUse final
	{
		RayTracingPipelineHandle Handle;
		void* Native = nullptr;
	};

	struct RecordedFramebufferUse final
	{
		FramebufferHandle Handle;
		void* Native = nullptr;
	};
}
