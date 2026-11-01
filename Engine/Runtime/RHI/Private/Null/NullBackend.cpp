#include "NullBackend.h"

#include <Array.hpp>
#include <ByteOperations.hpp>
#include <MemoryResource.h>
#include <Numeric.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <UniquePtr.hpp>
#include <Utility.hpp>

namespace rox::nullrhi
{
	using namespace core;

	using namespace rox::rhi;

	constexpr uint64 MAXIMUM_BUFFER_BYTES = 256ull * 1024ull * 1024ull;

	struct Buffer final
	{
		explicit Buffer(MemoryResource& resource) noexcept
			: Bytes(resource)
		{
		}

		Array<Byte> Bytes;
	};

	struct Texture final
	{
		explicit Texture(MemoryResource& resource) noexcept
			: Bytes(resource)
		{
		}

		Array<Byte> Bytes;
		TextureDesc Descriptor;
	};

	[[nodiscard]] usize subresourceOffset(const TextureDesc& desc, TextureSubresource subresource) noexcept
	{
		usize offset = 0;
		const usize bytesPerPixel = GetFormatInfo(desc.Format).BytesPerBlock;

		for (uint32 layer = 0; layer <= subresource.ArrayLayer; ++layer)
		{
			for (uint32 mip = 0; mip < desc.MipLevels; ++mip)
			{
				if (layer == subresource.ArrayLayer && mip == subresource.MipLevel)
				{
					return offset;
				}

				const uint32 width = (desc.Width >> mip) ? (desc.Width >> mip) : 1;
				const uint32 height = (desc.Height >> mip) ? (desc.Height >> mip) : 1;
				const uint32 blockSize = GetFormatInfo(desc.Format).BlockSize;
				const uint32 depth = (desc.Depth >> mip) ? (desc.Depth >> mip) : 1;
				offset += static_cast<usize>((width + blockSize - 1) / blockSize) * ((height + blockSize - 1) / blockSize) * bytesPerPixel * depth;
			}
		}

		return offset;
	}

	struct CommandList final
	{
		struct Command final
		{
			bool bTexture = false;
			bool bClear = false;

			NativeCopy Buffer;
			NativeTextureCopy Texture;

			void* ClearTarget = nullptr;
			ClearColor Color;
			TextureSubresource ClearSubresource;

			bool bBufferClear = false;
			void* ClearBuffer = nullptr;
			uint32 ClearValue = 0;

			bool bWrite = false;
			void* WriteTarget = nullptr;
			struct Buffer* WriteSource = nullptr;
			uint64 WriteOffset = 0;

			bool bTextureWrite = false;
			struct Texture* WriteTexture = nullptr;
			TextureSubresource WriteSubresource;

			bool bTextureBuffer = false;
			bool bBufferToTexture = false;
			NativeTextureBufferCopy TextureBuffer;
		};

		explicit CommandList(MemoryResource& resource) noexcept
			: Resource(resource)
			, Commands(resource)
			, Temporary(resource)
		{
		}

		~CommandList() noexcept
		{
			for (Buffer* buffer : Temporary)
			{
				DestroyObject(Resource, buffer);
			}
		}

		MemoryResource& Resource;

		Array<Command> Commands;
		bool bPrepared = false;

		Array<Buffer*> Temporary;
	};

	struct State final
	{
		explicit State(MemoryResource& resource) noexcept
			: Resource(resource)
		{
		}

		MemoryResource& Resource;
		uint64 LastSerial = 0;
	};

	[[nodiscard]] Error allocationError() noexcept
	{
		return MakeError(EErrorCode::ExternalFailure, "Rhi.Allocation", "The Null backend could not allocate storage.");
	}

	[[nodiscard]] Result<uint64> nextSerial(State& state)
	{
		if (state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			return Result<uint64>::Failure(EErrorCode::ExternalFailure, "Rhi.SubmissionLimit", "The copy queue serial is exhausted.");
		}

		return Result<uint64>::Success(++state.LastSerial);
	}

	[[nodiscard]] EFormatSupport QueryFormatSupport(const void*, EFormat) noexcept
	{
		return EFormatSupport::None;
	}

	[[nodiscard]] Result<uint32> QueryTextureSampleCounts(const void*, const TextureDesc&)
	{
		return Result<uint32>::Success(1);
	}

	[[nodiscard]] bool SupportsColorResolve(const void*, EFormat) noexcept
	{
		return false;
	}

	[[nodiscard]] Result<MemoryRequirements> GetBufferMemoryRequirements(const void*, const BufferDesc& desc)
	{
		return Result<MemoryRequirements>::Success({ desc.ByteCount, 1 });
	}

	[[nodiscard]] Result<uint32> QueryMemoryBudget(const void*, Span<MemoryBudget>)
	{
		return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.MemoryBudget", "The Null backend has no native memory budget.");
	}

	[[nodiscard]] Result<OpacityMicromapBuildSizes> GetOpacityMicromapBuildSizes(const void*, Span<const OpacityMicromapUsageCount>, EOpacityMicromapBuildFlags)
	{
		return Result<OpacityMicromapBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.NullOpacityMicromap", "The Null backend has no opacity micromaps.");
	}

	[[nodiscard]] Result<void*> CreateOpacityMicromap(void*, const OpacityMicromapDesc&, void*)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullOpacityMicromap", "The Null backend has no opacity micromaps.");
	}

	[[nodiscard]] Result<void> DestroyOpacityMicromap(void*, void*, void*)
	{
		return Result<void>::Success();
	}

	void DestroyOpacityMicromapNow(void*, void*) noexcept {}

	[[nodiscard]] Result<void> RecordBuildOpacityMicromap(void*, void*, void*, const OpacityMicromapBuildDesc&, void*, uint64, void*, uint64, void*, uint64)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullOpacityMicromap", "The Null backend has no opacity micromaps.");
	}

	[[nodiscard]] Result<AccelStructBuildSizes> GetAccelStructBuildSizes(
		const void*,
		const AccelStructBuildDesc&,
		Span<const NativeAccelTriangleGeometry>,
		Span<const NativeAccelAabbGeometry>)
	{
		return Result<AccelStructBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.NullAccelStruct", "The Null backend has no acceleration structures.");
	}

	[[nodiscard]] Result<void*> CreateAccelStruct(void*, const AccelStructDesc&, void*)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullAccelStruct", "The Null backend has no acceleration structures.");
	}

	[[nodiscard]] Result<void> DestroyAccelStruct(void*, void*)
	{
		return Result<void>::Success();
	}

	void DestroyAccelStructNow(void*, void*) noexcept {}

	[[nodiscard]] uint64 GetAccelStructDeviceAddress(const void*, const void*) noexcept
	{
		return 0;
	}

	[[nodiscard]] Result<void> RecordBuildBottomLevelAccelStruct(
		void*,
		void*,
		void*,
		const AccelStructBuildDesc&,
		Span<const NativeAccelTriangleGeometry>,
		Span<const NativeAccelAabbGeometry>,
		void*,
		uint64)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullAccelStruct", "The Null backend has no acceleration structures.");
	}

	[[nodiscard]] Result<void> RecordBuildTopLevelAccelStruct(void*, void*, void*, const AccelStructBuildDesc&, Span<const NativeAccelInstance>, void*, uint64)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullAccelStruct", "The Null backend has no acceleration structures.");
	}

	[[nodiscard]] Result<void> RecordBuildTopLevelAccelStructFromBuffer(void*, void*, void*, const AccelStructBuildDesc&, void*, uint64, void*, uint64)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullAccelStruct", "The Null backend has no acceleration structures.");
	}

	[[nodiscard]] Result<void> RecordSetAccelStructState(void*, void*, void*, EAccelStructState)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullAccelStruct", "The Null backend has no acceleration structures.");
	}

	[[nodiscard]] Result<MemoryRequirements> GetTextureMemoryRequirements(const void*, const TextureDesc&)
	{
		return Result<MemoryRequirements>::Failure(EErrorCode::Unsupported, "Rhi.NullHeap", "The Null backend does not expose texture placement requirements.");
	}

	[[nodiscard]] Result<void*> CreateHeap(void*, const HeapDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullHeap", "The Null backend does not create GPU heaps.");
	}

	[[nodiscard]] Result<void> DestroyHeap(void*, void*)
	{
		return Result<void>::Success();
	}

	void DestroyHeapNow(void*, void*) noexcept {}

	[[nodiscard]] Result<void*> CreateBuffer(State* opaque, const BufferDesc& desc)
	{
		State& state = *opaque;
		Buffer* buffer = CreateObject<Buffer>(state.Resource, state.Resource);

		if (buffer == nullptr)
		{
			return Result<void*>::Failure(allocationError());
		}

		if (!buffer->Bytes.ResizeUninitialized(static_cast<usize>(desc.ByteCount)))
		{
			DestroyObject(state.Resource, buffer);

			return Result<void*>::Failure(allocationError());
		}

		SetBytes(buffer->Bytes.Data(), 0, buffer->Bytes.Size());

		return Result<void*>::Success(buffer);
	}

	[[nodiscard]] Result<void*> ImportBuffer(void*, const BufferDesc&, void*)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NativeBuffer", "The Null backend has no native buffers.");
	}

	[[nodiscard]] void* GetNativeBuffer(const void*) noexcept
	{
		return nullptr;
	}

	[[nodiscard]] Result<void*> CreatePlacedBuffer(void*, const BufferDesc&, void*, uint64)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullHeap", "The Null backend does not create placed buffers.");
	}

	[[nodiscard]] uint64 GetBufferLastUse(const void*) noexcept
	{
		return 0;
	}

	[[nodiscard]] uint64 GetBufferDeviceAddress(const void*, const void*) noexcept
	{
		return 0;
	}

	[[nodiscard]] Result<void> DestroyBuffer(State* opaque, void* native)
	{
		DestroyBufferNow(opaque, native);

		return Result<void>::Success();
	}

	void DestroyBufferNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<Buffer*>(native));
	}

	[[nodiscard]] Result<Byte*> MapBuffer(void*, void* native, uint64 offset, uint64, uint32)
	{
		Buffer& buffer = *static_cast<Buffer*>(native);

		return Result<Byte*>::Success(buffer.Bytes.Data() + offset);
	}

	[[nodiscard]] Result<void> UnmapBuffer(void*, void*)
	{
		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreateTexture(State* opaque, const TextureDesc& desc)
	{
		State& state = *opaque;
		Texture* texture = CreateObject<Texture>(state.Resource, state.Resource);

		if (texture == nullptr)
		{
			return Result<void*>::Failure(allocationError());
		}

		texture->Descriptor = desc;
		const FormatInfo& format = GetFormatInfo(desc.Format);
		usize byteCount = 0;

		for (uint32 mip = 0; mip < desc.MipLevels; ++mip)
		{
			const uint32 width = (desc.Width >> mip) ? (desc.Width >> mip) : 1;
			const uint32 height = (desc.Height >> mip) ? (desc.Height >> mip) : 1;
			const uint32 depth = (desc.Depth >> mip) ? (desc.Depth >> mip) : 1;
			byteCount += static_cast<usize>((width + format.BlockSize - 1) / format.BlockSize) *
				((height + format.BlockSize - 1) / format.BlockSize) * format.BytesPerBlock * desc.ArrayLayers * depth;
		}

		if (!texture->Bytes.ResizeUninitialized(byteCount))
		{
			DestroyObject(state.Resource, texture);

			return Result<void*>::Failure(allocationError());
		}

		SetBytes(texture->Bytes.Data(), 0, byteCount);

		return Result<void*>::Success(texture);
	}

	[[nodiscard]] Result<void*> ImportTexture(void*, const TextureDesc&, void*, ENativeTextureContents)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NativeTexture", "The Null backend has no native textures.");
	}

	[[nodiscard]] void* GetNativeTexture(const void*) noexcept
	{
		return nullptr;
	}

	[[nodiscard]] Result<void*> CreatePlacedTexture(void*, const TextureDesc&, void*, uint64)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullHeap", "The Null backend does not create placed textures.");
	}

	[[nodiscard]] uint64 GetTextureLastUse(const void*) noexcept
	{
		return 0;
	}

	[[nodiscard]] Result<void> DestroyTexture(State* opaque, void* native)
	{
		DestroyTextureNow(opaque, native);

		return Result<void>::Success();
	}

	void DestroyTextureNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<Texture*>(native));
	}

	[[nodiscard]] Result<void*> CreateTextureView(void*, void*, const TextureDesc&, const TextureViewDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullTextureView", "The Null backend does not create GPU texture views.");
	}

	void DestroyTextureView(void*, void*) noexcept {}

	[[nodiscard]] Result<void*> CreateBufferView(void*, void*, const BufferViewDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullBufferView", "The Null backend does not create GPU buffer views.");
	}

	void DestroyBufferView(void*, void*) noexcept {}

	[[nodiscard]] Result<void*> CreateSampler(void*, const SamplerDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullSampler", "The Null backend does not create GPU samplers.");
	}

	void DestroySampler(void*, void*) noexcept {}

	[[nodiscard]] Result<uint64> UploadBuffer(State* opaque, void* native, uint64 offset, Span<const Byte> data)
	{
		State& state = *opaque;

		if (state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			return nextSerial(state);
		}

		Buffer& buffer = *static_cast<Buffer*>(native);
		CopyBytes(buffer.Bytes.Data() + offset, data.Data(), data.Size());

		return nextSerial(state);
	}

	[[nodiscard]] Result<void> ReadBuffer(void*, void* native, uint64 offset, Span<Byte> output, uint32)
	{
		Buffer& buffer = *static_cast<Buffer*>(native);
		CopyBytes(output.Data(), buffer.Bytes.Data() + offset, output.Size());

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint64> UploadTexture(State* opaque, void* native, const TextureDesc& desc, TextureSubresource subresource, Span<const Byte> data)
	{
		State& state = *opaque;

		if (state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			return nextSerial(state);
		}

		Texture& texture = *static_cast<Texture*>(native);
		CopyBytes(texture.Bytes.Data() + subresourceOffset(desc, subresource), data.Data(), data.Size());

		return nextSerial(state);
	}

	[[nodiscard]] Result<void> ReadTexture(void*, void* native, const TextureDesc& desc, TextureSubresource subresource, Span<Byte> output, uint32)
	{
		Texture& texture = *static_cast<Texture*>(native);
		CopyBytes(output.Data(), texture.Bytes.Data() + subresourceOffset(desc, subresource), output.Size());

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreateCommandList(State* opaque, EQueue)
	{
		State& state = *opaque;
		CommandList* list = CreateObject<CommandList>(state.Resource, state.Resource);

		if (list == nullptr)
		{
			return Result<void*>::Failure(allocationError());
		}

		return Result<void*>::Success(list);
	}

	void* GetNativeCommandList(const void*) noexcept
	{
		return nullptr;
	}

	void InvalidateCommandState(void*) noexcept {}

	void SetAutomaticBarriers(void*, bool) noexcept {}

	[[nodiscard]] Result<void> SetBufferUavBarriers(void*, void*, bool)
	{
		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> SetTextureUavBarriers(void*, void*, bool)
	{
		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBeginMarker(void*, void*, const char*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullMarker", "The Null backend has no GPU debug markers.");
	}

	[[nodiscard]] Result<void> RecordEndMarker(void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullMarker", "The Null backend has no GPU debug markers.");
	}

	[[nodiscard]] Result<void*> CreateGpuTimer(void*)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullTimer", "The Null backend has no GPU timestamps.");
	}

	void DestroyGpuTimer(void*, void*) noexcept {}

	[[nodiscard]] Result<void> RecordGpuTimerBegin(void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullTimer", "The Null backend has no GPU timestamps.");
	}

	[[nodiscard]] Result<void> RecordGpuTimerEnd(void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullTimer", "The Null backend has no GPU timestamps.");
	}

	[[nodiscard]] Result<double> ReadGpuTimer(void*, void*)
	{
		return Result<double>::Failure(EErrorCode::Unsupported, "Rhi.NullTimer", "The Null backend has no GPU timestamps.");
	}

	[[nodiscard]] Result<void> RecordBufferCopy(void*, void* opaque, const NativeCopy& copy)
	{
		CommandList& list = *static_cast<CommandList*>(opaque);

		if (!list.Commands.Add({ false, false, copy, {}, nullptr, {} }))
		{
			return Result<void>::Failure(allocationError());
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferWrite(State* opaque, void* native, void* destination, uint64 offset, Span<const Byte> data)
	{
		State& state = *opaque;
		CommandList& list = *static_cast<CommandList*>(native);
		Buffer* staging = CreateObject<Buffer>(state.Resource, state.Resource);

		if (staging == nullptr)
		{
			return Result<void>::Failure(allocationError());
		}

		if (!staging->Bytes.ResizeUninitialized(data.Size()))
		{
			DestroyObject(state.Resource, staging);

			return Result<void>::Failure(allocationError());
		}

		CopyBytes(staging->Bytes.Data(), data.Data(), data.Size());

		if (!list.Temporary.Add(staging))
		{
			DestroyObject(state.Resource, staging);

			return Result<void>::Failure(allocationError());
		}

		CommandList::Command command{};
		command.bWrite = true;
		command.WriteTarget = destination;
		command.WriteSource = staging;
		command.WriteOffset = offset;

		if (!list.Commands.Add(command))
		{
			return Result<void>::Failure(allocationError());
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferClear(void*, void* opaque, void* target, uint64, uint32 value)
	{
		CommandList& list = *static_cast<CommandList*>(opaque);
		CommandList::Command command{};
		command.bBufferClear = true;
		command.ClearBuffer = target;
		command.ClearValue = value;

		if (!list.Commands.Add(command))
		{
			return Result<void>::Failure(allocationError());
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureCopy(void*, void* opaque, const NativeTextureCopy& copy)
	{
		CommandList& list = *static_cast<CommandList*>(opaque);

		if (!list.Commands.Add({ true, false, {}, copy, nullptr, {} }))
		{
			return Result<void>::Failure(allocationError());
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> recordTextureBuffer(void* native, const NativeTextureBufferCopy& copy, bool bUpload)
	{
		CommandList& list = *static_cast<CommandList*>(native);
		CommandList::Command command{};
		command.bTextureBuffer = true;
		command.bBufferToTexture = bUpload;
		command.TextureBuffer = copy;

		if (!list.Commands.Add(command))
		{
			return Result<void>::Failure(allocationError());
		}

		return Result<void>::Success();
	}

	[[nodiscard]] CommandListStatistics GetCommandListStatistics(const void*) noexcept
	{
		return {};
	}

	bool HasMissingManualState(const void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	bool SupportsTextureResolve(const void*, const TextureDesc&, ETextureResolveMode, ETextureResolveMode) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	Result<void> PrepareTextureResolve(void*, void*, NativeTextureResolve&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<CoopVecFeatures> QueryCoopVecFeatures(const void*, Span<CoopVecFormatCombo>)
	{
		return Result<CoopVecFeatures>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<uint64> GetCoopVecMatrixSize(const void*, ECoopVecDataType, ECoopVecMatrixLayout, uint32, uint32)
	{
		return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	void PropagateAccelStructLastUse(const void*, const void*, void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
	}

	void PropagateAccelStructLastUseToBuffer(const void*, const void*, void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
	}

	Result<uint64> GetBottomLevelAccelStructCompactedSize(void*, void*)
	{
		return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordCompactBottomLevelAccelStruct(void*, void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<ClusterOperationSizeInfo> GetClusterOperationSizeInfo(const void*, const ClusterOperationParams&)
	{
		return Result<ClusterOperationSizeInfo>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordClusterOperation(void*, void*, const ClusterOperationDesc&, void*, void*, void*, void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	uint64 GetHeapCommittedBytes(const void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	void PropagateBufferLastUse(const void*, const void*, void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
	}

	Result<void*> CreateSamplerFeedbackTexture(void*, void*, const TextureDesc&, const SamplerFeedbackDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void*> ImportSamplerFeedbackTexture(void*, void*, const TextureDesc&, const SamplerFeedbackDesc&, void*)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<TextureTiling> GetTextureTiling(const void*, const void*, Span<TextureSubresourceTiling>)
	{
		return Result<TextureTiling>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<uint64> UpdateTextureTileMappings(void*, void*, const TextureDesc&, Span<const NativeTextureTileMapping>, EQueue)
	{
		return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> PrepareTextureTileMappings(void*, void*, const TextureDesc&, Span<const NativeTextureTileMapping>, EQueue, MemoryBlock&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	NativePreparedSubmissionResult SubmitPreparedTextureMappings(void*, MemoryBlock&, Span<const NativePreparedSubmissionWait>)
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	void DestroyPreparedTextureMappings(void*, MemoryBlock&) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
	}

	[[nodiscard]] Result<void*> CreateDescriptorTable(void*, const DescriptorTableDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullDescriptorTable", "The Null backend has no shader descriptors.");
	}

	uint32 GetDescriptorTableDeviceHeapIndex(const void*, const void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	[[nodiscard]] Result<void> WriteDescriptorTable(void*, void*, uint32, void*, void*, void*, void*, BufferRange)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullDescriptorTable", "The Null backend has no shader descriptors.");
	}

	[[nodiscard]] Result<void> CopyDescriptorTableEntries(void*, const void*, uint32, void*, uint32, uint32)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullDescriptorTable", "The Null backend has no shader descriptors.");
	}

	void DestroyDescriptorTable(void*, void*) noexcept {}

	[[nodiscard]] Result<void*> CreateSwapChain(void*, const SwapChainDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullPresent", "The Null backend has no window presentation.");
	}

	[[nodiscard]] Result<void> ResizeSwapChain(void*, void*, uint32, uint32)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullPresent", "The Null backend has no window presentation.");
	}

	Result<void> TryResizeSwapChain(void* state, void* swapChain, uint32 width, uint32 height)
	{
		return ResizeSwapChain(state, swapChain, width, height);
	}

	[[nodiscard]] Result<void*> AcquireSwapChainImage(void*, void*, uint32)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullPresent", "The Null backend has no window presentation.");
	}

	[[nodiscard]] Result<uint64> PresentSwapChainImage(void*, void*)
	{
		return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.NullPresent", "The Null backend has no window presentation.");
	}

	[[nodiscard]] Result<uint64> Present(void*, void*, void*)
	{
		return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.NullPresent", "The Null backend has no window presentation.");
	}

	uint64 GetPresentCopySerial(const void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	[[nodiscard]] Result<void> DestroySwapChain(void*, void*)
	{
		return Result<void>::Success();
	}

	Result<void> TryDestroySwapChain(void* state, void* swapChain)
	{
		return DestroySwapChain(state, swapChain);
	}

	void DestroySwapChainNow(void*, void*) noexcept {}

	[[nodiscard]] Result<void> RecordShaderMemoryBarrier(void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullBarrier", "The Null backend has no shader execution.");
	}

	[[nodiscard]] Result<void> RecordShaderBufferBarrier(void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullBarrier", "The Null backend has no shader execution.");
	}

	[[nodiscard]] Result<void> RecordShaderTextureBarrier(void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullBarrier", "The Null backend has no shader execution.");
	}

	[[nodiscard]] Result<void> RecordPrepareTextureForShaderAccess(void*, void*, void*, const TextureViewDesc*, ETextureState)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullBarrier", "The Null backend has no shader execution.");
	}

	[[nodiscard]] Result<void> RecordSetTextureState(void*, void*, void*, const TextureSubresource*, ETextureState)
	{
		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBeginTrackingTextureState(void*, void*, void*, const TextureSubresource*, ETextureState)
	{
		return Result<void>::Success();
	}

	Result<void> RecordSetSamplerFeedbackState(void*, void*, void*, ESamplerFeedbackState)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	[[nodiscard]] ETrackedResourceState GetTextureSubresourceState(const void*, const void*, const void*, TextureSubresource) noexcept
	{
		return ETrackedResourceState::Unknown;
	}

	[[nodiscard]] Result<void> RecordSetBufferState(void*, void*, void*, EBufferState)
	{
		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBeginTrackingBufferState(void*, void*, void*, EBufferState)
	{
		return Result<void>::Success();
	}

	Result<void> RecordCoopVecMatrices(void*, void*, Span<const NativeCoopVecMatrixConversion>, bool)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	[[nodiscard]] Result<void> RecordBufferToTexture(void*, void* native, const NativeTextureBufferCopy& copy)
	{
		return recordTextureBuffer(native, copy, true);
	}

	[[nodiscard]] Result<void> RecordTextureToBuffer(void*, void* native, const NativeTextureBufferCopy& copy)
	{
		return recordTextureBuffer(native, copy, false);
	}

	Result<void> PrepareTextureBufferCopy(void*, NativeTextureBufferCopy&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	[[nodiscard]] Result<void> RecordTextureWrite(State* opaque, void* native, void* destination, const TextureDesc&, TextureSubresource subresource, Span<const Byte> data)
	{
		State& state = *opaque;
		CommandList& list = *static_cast<CommandList*>(native);
		Buffer* staging = CreateObject<Buffer>(state.Resource, state.Resource);

		if (staging == nullptr)
		{
			return Result<void>::Failure(allocationError());
		}

		if (!staging->Bytes.ResizeUninitialized(data.Size()))
		{
			DestroyObject(state.Resource, staging);

			return Result<void>::Failure(allocationError());
		}

		CopyBytes(staging->Bytes.Data(), data.Data(), data.Size());

		if (!list.Temporary.Add(staging))
		{
			DestroyObject(state.Resource, staging);

			return Result<void>::Failure(allocationError());
		}

		CommandList::Command command{};
		command.bTextureWrite = true;
		command.WriteTexture = static_cast<Texture*>(destination);
		command.WriteSubresource = subresource;
		command.WriteSource = staging;

		if (!list.Commands.Add(command))
		{
			return Result<void>::Failure(allocationError());
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureResolve(void*, void*, const NativeTextureResolve&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullResolve", "The Null backend does not resolve multisampled textures.");
	}

	[[nodiscard]] Result<void> RecordTextureClear(void*, void* opaque, void* target, const TextureDesc&, TextureSubresource subresource, const ClearColor& color)
	{
		CommandList& list = *static_cast<CommandList*>(opaque);
		CommandList::Command command{};
		command.bClear = true;
		command.ClearTarget = target;
		command.Color = color;
		command.ClearSubresource = subresource;

		if (!list.Commands.Add(command))
		{
			return Result<void>::Failure(allocationError());
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureUIntClear(void*, void*, void*, const TextureDesc&, TextureSubresource, uint32)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullUIntClear", "The Null backend does not create storage textures.");
	}

	Result<void> RecordSamplerFeedbackClear(void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordSamplerFeedbackDecode(void*, void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordSamplerFeedbackDecodeTexture(void*, void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	[[nodiscard]] Result<void> RecordDepthClear(void*, void*, void*, const TextureDesc&, TextureSubresource, DepthStencilClear)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullDepth", "The Null backend does not create depth targets.");
	}

	[[nodiscard]] Result<void*> CreateComputePipeline(void*, const NativeComputePipelineDesc&, Span<const NativeBindingLayoutDesc>)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullCompute", "The Null backend does not execute shaders.");
	}

	[[nodiscard]] Result<void> DestroyComputePipeline(void*, void*)
	{
		return Result<void>::Success();
	}

	void DestroyComputePipelineNow(void*, void*) noexcept {}

	[[nodiscard]] Result<void> RecordComputeDispatch(
		void*,
		void*,
		void*,
		Span<const NativeComputeBuffer>,
		Span<const NativeComputeTexture>,
		uint32,
		uint32,
		uint32,
		void*,
		uint64,
		Span<const Byte>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullCompute", "The Null backend does not execute shaders.");
	}

	Result<void> RecordComputeDispatchMultiple(
		void*,
		void*,
		void*,
		Span<const NativeBindingSet>,
		uint32,
		uint32,
		uint32,
		void*,
		uint64,
		Span<const Byte>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	[[nodiscard]] Result<void*> CreateGraphicsPipeline(void*, const NativeGraphicsPipelineDesc&, Span<const NativeGraphicsBindingLayoutDesc>)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not execute graphics shaders.");
	}

	[[nodiscard]] Result<void> DestroyGraphicsPipeline(void*, void*)
	{
		return Result<void>::Success();
	}

	void DestroyGraphicsPipelineNow(void*, void*) noexcept {}

	[[nodiscard]] Result<void*> CreateFramebuffer(void*, const NativeFramebufferDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	[[nodiscard]] Result<void> DestroyFramebuffer(void*, void*)
	{
		return Result<void>::Success();
	}

	void DestroyFramebufferNow(void*, void*) noexcept {}

	[[nodiscard]] Result<void> RecordBeginFramebuffer(void*, void*, void*, DepthAttachmentOptions)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	[[nodiscard]] Result<void> RecordSetViewport(void*, void*, Span<const ViewportDesc>)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	[[nodiscard]] Result<void> RecordSetScissor(void*, void*, Span<const ScissorDesc>)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	[[nodiscard]] Result<void> RecordBindExtraVertexBuffers(void*, void*, Span<const NativeVertexBuffer>)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	[[nodiscard]] Result<void> RecordDraw(
		void*,
		void*,
		void*,
		void*,
		uint64,
		uint64,
		uint32,
		Span<const NativeSampledTexture>,
		Span<const NativeStorageTexture>,
		Span<const NativeGraphicsBuffer>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		const GraphicsState&,
		const DrawArguments&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	[[nodiscard]] Result<void> RecordDrawIndexed(
		void*,
		void*,
		void*,
		void*,
		void*,
		uint64,
		uint64,
		uint64,
		uint32,
		Span<const NativeSampledTexture>,
		Span<const NativeStorageTexture>,
		Span<const NativeGraphicsBuffer>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		const GraphicsState&,
		const DrawIndexedArguments&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	[[nodiscard]] Result<void> RecordDrawIndirect(
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		uint64,
		uint64,
		uint32,
		Span<const NativeSampledTexture>,
		Span<const NativeStorageTexture>,
		Span<const NativeGraphicsBuffer>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		const GraphicsDrawIndirect&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	[[nodiscard]] Result<void> RecordDrawIndexedIndirect(
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		uint64,
		uint64,
		uint64,
		uint32,
		Span<const NativeSampledTexture>,
		Span<const NativeStorageTexture>,
		Span<const NativeGraphicsBuffer>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		const GraphicsDrawIndexedIndirect&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	[[nodiscard]] Result<void> RecordEndFramebuffer(void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullGraphics", "The Null backend does not render.");
	}

	bool ReserveSubmission(void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	[[nodiscard]] Result<uint64> SubmitCommandList(
		State* opaque,
		void* native,
		Span<const RecordedCopy>,
		Span<const RecordedTextureCopy>,
		Span<const RecordedTextureUse>,
		Span<const RecordedBufferUse>,
		Span<const RecordedAccelUse>,
		Span<const RecordedPipelineUse>,
		Span<const RecordedGraphicsPipelineUse>,
		Span<const RecordedRayPipelineUse>,
		Span<const RecordedFramebufferUse>)
	{
		State& state = *opaque;
		CommandList& list = *static_cast<CommandList*>(native);

		if (state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			DestroyObject(state.Resource, &list);

			return nextSerial(state);
		}

		for (const CommandList::Command& command : list.Commands)
		{
			if (command.bTextureBuffer)
			{
				const NativeTextureBufferCopy& copy = command.TextureBuffer;
				Buffer& buffer = *static_cast<Buffer*>(copy.Buffer);
				Texture& texture = *static_cast<Texture*>(copy.Texture);
				const FormatInfo& format = GetFormatInfo(texture.Descriptor.Format);
				const usize rowBytes = ((copy.Footprint.Width + format.BlockSize - 1) / format.BlockSize) * format.BytesPerBlock;
				const usize textureOffset = subresourceOffset(texture.Descriptor, copy.Subresource);

				for (uint32 slice = 0; slice < copy.Footprint.Depth; ++slice)
				{
					for (uint32 row = 0; row < copy.Footprint.RowCount; ++row)
					{
						const usize rowIndex = static_cast<usize>(slice) * copy.Footprint.RowCount + row;
						Byte* const textureRow = texture.Bytes.Data() + textureOffset + rowIndex * rowBytes;
						Byte* const bufferRow = buffer.Bytes.Data() + copy.BufferOffsetBytes + rowIndex * copy.Footprint.RowPitchBytes;

						if (command.bBufferToTexture)
						{
							CopyBytes(textureRow, bufferRow, rowBytes);
						}
						else
						{
							CopyBytes(bufferRow, textureRow, rowBytes);
						}
					}
				}
			}
			else if (command.bTextureWrite)
			{
				struct Texture& texture = *command.WriteTexture;
				CopyBytes(texture.Bytes.Data() + subresourceOffset(texture.Descriptor, command.WriteSubresource),
					command.WriteSource->Bytes.Data(), command.WriteSource->Bytes.Size());
			}
			else if (command.bWrite)
			{
				Buffer& buffer = *static_cast<Buffer*>(command.WriteTarget);
				CopyBytes(buffer.Bytes.Data() + command.WriteOffset, command.WriteSource->Bytes.Data(),
					command.WriteSource->Bytes.Size());
			}
			else if (command.bBufferClear)
			{
				Buffer& buffer = *static_cast<Buffer*>(command.ClearBuffer);

				for (usize offset = 0; offset < buffer.Bytes.Size(); offset += 4)
				{
					CopyBytes(buffer.Bytes.Data() + offset, &command.ClearValue, 4);
				}
			}
			else if (command.bClear)
			{
				Texture& texture = *static_cast<Texture*>(command.ClearTarget);
				const Byte rgba[4]{ static_cast<Byte>(static_cast<uint8>(command.Color.GetFloat()[0] * 255.0f + 0.5f)),
										 static_cast<Byte>(static_cast<uint8>(command.Color.GetFloat()[1] * 255.0f + 0.5f)),
										 static_cast<Byte>(static_cast<uint8>(command.Color.GetFloat()[2] * 255.0f + 0.5f)),
										 static_cast<Byte>(static_cast<uint8>(command.Color.GetFloat()[3] * 255.0f + 0.5f)) };
				const uint32 width = texture.Descriptor.Width >> command.ClearSubresource.MipLevel ? texture.Descriptor.Width >> command.ClearSubresource.MipLevel : 1;
				const uint32 height = texture.Descriptor.Height >> command.ClearSubresource.MipLevel ? texture.Descriptor.Height >> command.ClearSubresource.MipLevel : 1;
				const usize base = subresourceOffset(texture.Descriptor, command.ClearSubresource);

				for (usize index = 0; index < static_cast<usize>(width) * height * 4; index += 4)
				{
					CopyBytes(texture.Bytes.Data() + base + index, rgba, 4);
				}
			}
			else if (command.bTexture)
			{
				Texture& source = *static_cast<Texture*>(command.Texture.Source);
				Texture& destination = *static_cast<Texture*>(command.Texture.Destination);

				if (!command.Texture.bRegion)
				{
					CopyBytes(destination.Bytes.Data(), source.Bytes.Data(), source.Bytes.Size());
				}
				else
				{
					const NativeTextureCopy& region = command.Texture;
					const FormatInfo& format = GetFormatInfo(source.Descriptor.Format);
					const uint32 block = format.BlockSize;
					const uint32 sourceWidth = source.Descriptor.Width >> region.SourceMip ? source.Descriptor.Width >> region.SourceMip : 1;
					const uint32 destinationWidth = destination.Descriptor.Width >> region.DestinationMip ? destination.Descriptor.Width >> region.DestinationMip : 1;
					const uint32 sourceHeight = source.Descriptor.Height >> region.SourceMip ? source.Descriptor.Height >> region.SourceMip : 1;
					const uint32 destinationHeight = destination.Descriptor.Height >> region.DestinationMip ? destination.Descriptor.Height >> region.DestinationMip : 1;
					const usize sourcePitch = ((sourceWidth + block - 1) / block) * format.BytesPerBlock;
					const usize destinationPitch = ((destinationWidth + block - 1) / block) * format.BytesPerBlock;
					const usize rowBytes = ((region.Width + block - 1) / block) * format.BytesPerBlock;
					const usize sourceBase =
						subresourceOffset(source.Descriptor, { region.SourceMip, region.SourceLayer }) +
						static_cast<usize>(region.SourceZ) * sourcePitch * ((sourceHeight + block - 1) / block) +
						(region.SourceY / block) * sourcePitch + (region.SourceX / block) * format.BytesPerBlock;
					const usize destinationBase =
						subresourceOffset(destination.Descriptor, { region.DestinationMip, region.DestinationLayer }) +
						static_cast<usize>(region.DestinationZ) * destinationPitch *
						((destinationHeight + block - 1) / block) +
						(region.DestinationY / block) * destinationPitch + (region.DestinationX / block) * format.BytesPerBlock;
					const usize sourceSlicePitch = sourcePitch * ((sourceHeight + block - 1) / block);
					const usize destinationSlicePitch = destinationPitch * ((destinationHeight + block - 1) / block);

					for (uint32 slice = 0; slice < region.Depth; ++slice)
					{
						for (uint32 row = 0; row < (region.Height + block - 1) / block; ++row)
						{
							CopyBytes(
								destination.Bytes.Data() + destinationBase + slice * destinationSlicePitch + row * destinationPitch,
								source.Bytes.Data() + sourceBase + slice * sourceSlicePitch + row * sourcePitch, rowBytes);
						}
					}
				}
			}
			else
			{
				const NativeCopy& copy = command.Buffer;
				Buffer& source = *static_cast<Buffer*>(copy.Source);
				Buffer& destination = *static_cast<Buffer*>(copy.Destination);
				MoveBytes(destination.Bytes.Data() + copy.DestinationOffsetBytes, source.Bytes.Data() + copy.SourceOffsetBytes,
					static_cast<usize>(copy.ByteCount));
			}
		}

		DestroyObject(state.Resource, &list);

		return nextSerial(state);
	}

	NativePreparedSubmissionResult SubmitPreparedCommandList(
		void*,
		void*,
		const NativePreparedSubmissionDesc&,
		Span<const RecordedTextureUse>,
		Span<const RecordedBufferUse>,
		Span<const RecordedAccelUse>,
		Span<const RecordedPipelineUse>,
		Span<const RecordedGraphicsPipelineUse>,
		Span<const RecordedRayPipelineUse>,
		Span<const RecordedFramebufferUse>)
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	void DiscardCommandList(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<CommandList*>(native));
	}

	[[nodiscard]] Result<bool> IsComplete(const State* opaque, uint64 serial)
	{
		const State& state = *opaque;

		ASSERT(serial <= state.LastSerial, "The submission token is not known to this device.");

		return Result<bool>::Success(true);
	}

	[[nodiscard]] Result<bool> IsQueueComplete(const State* opaque, EQueue queue, uint64 serial)
	{
		ASSERT(queue == EQueue::Copy, "The submission token is not known to this device.");

		return IsComplete(opaque, serial);
	}

	[[nodiscard]] uint64 GetQueueLastSubmittedSerial(const State* opaque, EQueue queue) noexcept
	{
		return queue == EQueue::Copy ? opaque->LastSerial : 0;
	}

	[[nodiscard]] Result<void> Wait(State* opaque, EQueue queue, uint64 serial, uint32)
	{
		Result<bool> complete = IsQueueComplete(opaque, queue, serial);

		if (!complete)
		{
			return Result<void>::Failure(complete.GetError());
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> QueueWaitForSubmission(State* opaque, EQueue, EQueue executionQueue, uint64 serial)
	{
		Result<bool> complete = IsQueueComplete(opaque, executionQueue, serial);

		if (!complete)
		{
			return Result<void>::Failure(complete.GetError());
		}

		return Result<void>::Success();
	}

	void Collect(void*) noexcept {}

	[[nodiscard]] Result<void> WaitIdle(void*)
	{
		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> WaitIdleBounded(void*, uint32)
	{
		return Result<void>::Success();
	}

	bool HasRetirementProof(const void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	void Destroy(State* opaque) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, &state);
	}

	Result<void*> CreateRayTracingPipeline(void*, const NativeRayPipelineDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> DestroyRayTracingPipeline(void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	void DestroyRayTracingPipelineNow(void*, void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
	}

	Result<void> RecordRayDispatch(
		void*,
		void*,
		void*,
		const NativeShaderTableDesc&,
		Span<const NativeBindingSet>,
		Span<const NativeFixedBindingSet>,
		Span<const Byte>,
		void*,
		uint32,
		uint32,
		uint32)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordFixedBindingSets(void*, void*, void*, bool, Span<const NativeFixedBindingSet>)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<uint32> PrepareAccelBuild(void*, void*, const NativePreparedAccelBuild&)
	{
		return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordPreparedAccelBuild(void*, void*, uint32)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<uint32> PrepareAccelQuery(void*, void*, void*, uint64)
	{
		return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordPreparedAccelQuery(void*, void*, uint32)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	void CommitPreparedAccelQuery(void*, void*, uint64, EQueue, uint64, bool) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
	}

	void CancelPreparedAccelQuery(void*, void*, uint64) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
	}

	Result<AccelCompactionResult> TryGetAccelCompactedSize(void*, void*, uint64)
	{
		return Result<AccelCompactionResult>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<uint32> PrepareAccelCompact(void*, void*, void*, void*)
	{
		return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordPreparedAccelCompact(void*, void*, uint32)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<SharedPtr<PreparedShaderTable>> CreatePreparedShaderTable(void*, void*, const NativeShaderTableDesc&)
	{
		return Result<SharedPtr<PreparedShaderTable>>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<uint32> PrepareRayBinding(
		void*,
		void*,
		void*,
		const SharedPtr<PreparedShaderTable>&,
		Span<const NativeFixedBindingSet>,
		Span<const NativePreparedTextureAccess>)
	{
		return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordPreparedRayDispatch(void*, void*, void*, uint32, const RayDispatch&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	DeviceLimits GetLimits(const void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	Result<TextureBufferFootprint> GetTextureBufferFootprint(void*, const TextureDesc&, TextureSubresource)
	{
		return Result<TextureBufferFootprint>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	ExternalInteropCapabilities GetExternalInteropCapabilities(const void*)
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	Result<void*> CreateExternalBuffer(void*, const BufferDesc&, const NativeExternalMemoryView*)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void*> CreateExternalTexture(void*, const TextureDesc&, const NativeExternalMemoryView*)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<NativeExternalMemory> ExportExternalMemory(void*, void*, bool)
	{
		return Result<NativeExternalMemory>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void*> CreateExternalFence(void*, uint64, const ExternalHandleView*)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<ExternalFence> ExportExternalFence(void*, void*)
	{
		return Result<ExternalFence>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<uint64> GetExternalFenceValue(void*, void*)
	{
		return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	void DestroyExternalFence(void*, void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
	}

	Result<void> RecordExternalTransfer(void*, void*, void*, bool, void*, uint64, bool)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	void ExposeNativeQueues(void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
	}

	bool WasSubmissionAccepted(const void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	[[nodiscard]] Result<void> PrepareRecordingGroup(void*, Span<const NativePreparedList> lists)
	{
		for (const rhi::NativePreparedList& description : lists)
		{
			CommandList& list = *static_cast<CommandList*>(description.Commands);
			if (!list.Commands.Reserve(description.CommandCount))
			{
				return Result<void>::Failure(allocationError());
			}

			list.bPrepared = true;
		}

		return Result<void>::Success();
	}

	void ResetPreparedCommandList(void*, void* native) noexcept
	{
		static_cast<CommandList*>(native)->bPrepared = false;
	}

	[[nodiscard]] Result<uint32> PrepareBinding(
		void*,
		void*,
		void*,
		bool,
		Span<const NativeFixedBindingSet>,
		Span<const NativePreparedTextureAccess>,
		const NativePreparedVertexInput&,
		Span<const NativeDescriptorHeapBinding>,
		SharedPtr<NativeBindingProgram>*)
	{
		Error error;
		error.Code = EErrorCode::Unsupported;

		return Result<uint32>::Failure(Move(error));
	}

	[[nodiscard]] Result<void> RecordPreparedPhase(void*, void*, const NativePreparedPhase&)
	{
		return Result<void>::Success();
	}

	Result<void> RecordPreparedDispatch(void*, void*, void*, uint32, const ComputeDispatch&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	[[nodiscard]] Result<void> RecordPreparedMeshDispatch(void*, void*, void*, uint32, const MeshDispatch&)
	{
		Error error;
		error.Code = EErrorCode::Unsupported;

		return Result<void>::Failure(Move(error));
	}

	Result<void> RecordPreparedDraw(void*, void*, void*, uint32, const DrawArguments&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	[[nodiscard]] Result<void> RecordDrawIndexedArguments(void*, void*, void*, uint32, const DrawIndexedArguments&)
	{
		Error error;
		error.Code = EErrorCode::Unsupported;

		return Result<void>::Failure(Move(error));
	}

	Result<void> RecordPreparedRendering(void*, void*, void*, Span<const EAttachmentLoad>, Span<const ClearColor>, DepthAttachmentOptions)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	[[nodiscard]] Result<void> FinishPreparedCommandList(void*, void*)
	{
		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> QueueWaitForPreparedSubmission(State* opaque, EQueue, EQueue executionQueue, uint64 serial)
	{
		if (executionQueue != EQueue::Copy || serial > opaque->LastSerial)
		{
			Error error;
			error.Code = EErrorCode::InvalidArgument;

			return Result<void>::Failure(Move(error));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint32> PrepareIndirect(void*, void*, const NativePreparedIndirect&)
	{
		Error error;
		error.Code = EErrorCode::Unsupported;

		return Result<uint32>::Failure(Move(error));
	}

	[[nodiscard]] Result<void> RecordPreparedIndirect(void*, void*, void*, uint32, const NativePreparedIndirect&, Span<const Byte>)
	{
		Error error;
		error.Code = EErrorCode::Unsupported;

		return Result<void>::Failure(Move(error));
	}

	Result<void> RecordImmutableRayDispatch(void*, void*, void*, const SharedPtr<PreparedShaderTable>&, Span<const NativeFixedBindingSet>, const RayDispatch&)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<uint32> DescribePreparedNativeBoundary(const void*, const void*, const void*, bool) noexcept
	{
		return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> DiscardAcquiredSwapChainImage(void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordDeviceHeapUses(void*, void*, bool, Span<const NativeDescriptorHeapBinding>)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<BackendData> CreateNullBackend(MemoryResource& resource)
	{
		State* state = CreateObject<State>(resource, resource);

		if (state == nullptr)
		{
			return Result<BackendData>::Failure(allocationError());
		}

		GpuCapabilities capabilities{};
		capabilities.Features.bGraphics = false;
		capabilities.Features.bCompute = false;
		capabilities.Features.bCopy = true;
		capabilities.Limits.MaximumBufferByteCount = MAXIMUM_BUFFER_BYTES;

		return Result<BackendData>::Success({ state, capabilities });
	}
}
