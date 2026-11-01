#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] bool SupportsSamples(const State& state, DXGI_FORMAT format, uint32 sampleCount) noexcept
	{
		if (sampleCount == 1)
		{
			return true;
		}

		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
		levels.Format = format;
		levels.SampleCount = sampleCount;
		levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;

		return SUCCEEDED(state.Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))) && levels.NumQualityLevels != 0;
	}

	[[nodiscard]] Result<uint32> QueryTextureSampleCounts(const State* opaque, const TextureDesc& desc)
	{
		const State& state = *opaque;
		const DxgiFormatMapping& mapping = GetDxgiFormatMapping(desc.Format);
		const DXGI_FORMAT format = GetFormatInfo(desc.Format).bHasDepth ? mapping.RtvFormat : mapping.SrvFormat;
		D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
		support.Format = format;
		if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))))
		{
			return Result<uint32>::Success(0);
		}

		const D3D12_FORMAT_SUPPORT1 type = desc.Dimension == ETextureDimension::Texture1D ? D3D12_FORMAT_SUPPORT1_TEXTURE1D
			: desc.Dimension == ETextureDimension::Texture3D ? D3D12_FORMAT_SUPPORT1_TEXTURE3D
			: desc.Dimension == ETextureDimension::Cube ? D3D12_FORMAT_SUPPORT1_TEXTURECUBE
			: D3D12_FORMAT_SUPPORT1_TEXTURE2D;
		if ((support.Support1 & type) == 0)
		{
			return Result<uint32>::Success(0);
		}

		const bool bShaderRead = HasUsage(desc.Usage, ETextureUsage::ShaderRead);
		D3D12_FEATURE_DATA_FORMAT_SUPPORT readSupport = support;
		if (bShaderRead && mapping.SrvFormat != format)
		{
			readSupport = {};
			readSupport.Format = mapping.SrvFormat;
			if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &readSupport, sizeof(readSupport))))
			{
				return Result<uint32>::Success(0);
			}
		}

		uint32 mask = 1;
		if ((support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET) != 0 && (!bShaderRead || (readSupport.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD) != 0))
		{
			for (uint32 samples = 2; samples <= 8; samples *= 2)
			{
				if (SupportsSamples(state, format, samples) && (!bShaderRead || SupportsSamples(state, mapping.SrvFormat, samples)))
				{
					mask |= samples;
				}
			}
		}

		return Result<uint32>::Success(mask);
	}

	[[nodiscard]] bool SupportsColorResolve(const State* opaque, EFormat format) noexcept
	{
		const State& state = *opaque;
		D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
		support.Format = GetDxgiFormatMapping(format).RtvFormat;

		return SUCCEEDED(state.Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) && (support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE) != 0;
	}

	[[nodiscard]] bool SupportsTextureResolve(const State* opaque, const TextureDesc& desc, ETextureResolveMode mode, ETextureResolveMode stencilMode) noexcept
	{
		const State& state = *opaque;
		const FormatInfo& info = GetFormatInfo(desc.Format);
		if (!info.bHasDepth && mode == ETextureResolveMode::Average && stencilMode == ETextureResolveMode::None)
		{
			return info.Kind != EFormatKind::Integer && SupportsColorResolve(opaque, desc.Format);
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS2 options{};
		if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &options, sizeof(options))) || options.ProgrammableSamplePositionsTier < D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_1)
		{
			return false;
		}

		const bool bMainSupported = mode == ETextureResolveMode::None ||
			mode == ETextureResolveMode::Minimum ||
			mode == ETextureResolveMode::Maximum ||
			(mode == ETextureResolveMode::Average && info.Kind != EFormatKind::Integer);
		const bool bStencilSupported = stencilMode == ETextureResolveMode::None || (info.bHasStencil && (stencilMode == ETextureResolveMode::Minimum || stencilMode == ETextureResolveMode::Maximum));
		D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
		support.Format = GetDxgiFormatMapping(desc.Format).RtvFormat;

		return bMainSupported &&
			bStencilSupported &&
			SUCCEEDED(state.Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
			(support.Support1 & (info.bHasDepth ? D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL : D3D12_FORMAT_SUPPORT1_RENDER_TARGET)) != 0;
	}

	[[nodiscard]] Result<void> PrepareTextureResolve(void*, void* native, NativeTextureResolve& resolve)
	{
		Pending& pending = *static_cast<Pending*>(native);
		if ((GetFormatInfo(resolve.Format).bHasDepth || resolve.Mode != ETextureResolveMode::Average) && !pending.ResolveList && FAILED(pending.List.As(&pending.ResolveList)))
		{
			Error error;
			error.Code = EErrorCode::Unsupported;

			return Result<void>::Failure(Move(error));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint64> UploadTexture(State* opaque, void* native, const TextureDesc& desc, TextureSubresource subresource, Span<const Byte> data)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<uint64>::Failure(active.GetError());
		}

		Texture& texture = *static_cast<Texture*>(native);
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT rowCount = 0;
		UINT64 rowByteCount = 0;
		UINT64 stagingBytes = 0;
		const D3D12_RESOURCE_DESC nativeDesc = texture.Resource->GetDesc();
		const UINT subresourceIndex = subresource.ArrayLayer * desc.MipLevels + subresource.MipLevel + (subresource.Aspect == ETextureAspect::Stencil ? desc.MipLevels * desc.ArrayLayers : 0);
		const uint32 width = (desc.Width >> subresource.MipLevel) ? (desc.Width >> subresource.MipLevel) : 1;
		const uint32 height = (desc.Height >> subresource.MipLevel) ? (desc.Height >> subresource.MipLevel) : 1;
		const uint32 depth = (desc.Depth >> subresource.MipLevel) ? (desc.Depth >> subresource.MipLevel) : 1;
		state.Device->GetCopyableFootprints(&nativeDesc, subresourceIndex, 1, 0, &footprint, &rowCount, &rowByteCount, &stagingBytes);
		const FormatInfo& formatInfo = GetFormatInfo(GetTextureTransferFormat(desc.Format, subresource.Aspect));

		if (footprint.Footprint.Depth != depth ||
			rowCount != (height + formatInfo.BlockSize - 1) / formatInfo.BlockSize ||
			rowByteCount != static_cast<uint64>((width + formatInfo.BlockSize - 1) / formatInfo.BlockSize) * formatInfo.BytesPerBlock)
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12Texture", "The D3D12 texture footprint is incompatible."));
		}

		ComPtr<ID3D12Resource> staging;

		if (!CreateResource(state, stagingBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, staging))
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12Staging", "Could not create texture upload storage."));
		}

		void* mapped = nullptr;

		if (FAILED(staging->Map(0, nullptr, &mapped)))
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12Map", "Could not map texture upload storage."));
		}

		for (UINT slice = 0; slice < depth; ++slice)
		{
			for (UINT row = 0; row < rowCount; ++row)
			{
				const usize rowIndex = static_cast<usize>(slice) * rowCount + row;
				CopyBytes(static_cast<Byte*>(mapped) + rowIndex * footprint.Footprint.RowPitch,
					data.Data() + rowIndex * rowByteCount, static_cast<usize>(rowByteCount));
			}
		}

		staging->Unmap(0, nullptr);
		const EQueue queue = GetFormatInfo(desc.Format).bHasDepth ? EQueue::Graphics : EQueue::Copy;
		Result<Pending> started = Begin(state, queue);

		if (!started)
		{
			return Result<uint64>::Failure(started.GetError());
		}

		Pending pending = Move(started).TakeValue();

		if (!pending.Temporary.Add(staging))
		{
			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not track texture upload storage."));
		}

		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = staging.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = texture.Resource.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = subresourceIndex;
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST, subresourceIndex);
		pending.List->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON, subresourceIndex);
		Result<uint64> submitted = Execute(state, Move(pending));

		if (submitted)
		{
			RecordResourceUse(texture, queue, submitted.Value());
			if (texture.bUnknownAliases)
			{
				state.UnknownResourceUses[static_cast<uint32>(queue)] = submitted.Value();
			}
		}

		return submitted;
	}

	[[nodiscard]] Result<void> ReadTexture(State* opaque, void* native, const TextureDesc& desc, TextureSubresource subresource, Span<Byte> output, uint32 timeout)
	{
		State& state = *opaque;
		state.bSubmissionMayBePending = false;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return active;
		}

		Texture& texture = *static_cast<Texture*>(native);
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT rowCount = 0;
		UINT64 rowByteCount = 0;
		UINT64 stagingBytes = 0;
		const D3D12_RESOURCE_DESC nativeDesc = texture.Resource->GetDesc();
		const UINT subresourceIndex = subresource.ArrayLayer * desc.MipLevels + subresource.MipLevel + (subresource.Aspect == ETextureAspect::Stencil ? desc.MipLevels * desc.ArrayLayers : 0);
		const uint32 width = (desc.Width >> subresource.MipLevel) ? (desc.Width >> subresource.MipLevel) : 1;
		const uint32 height = (desc.Height >> subresource.MipLevel) ? (desc.Height >> subresource.MipLevel) : 1;
		const uint32 depth = (desc.Depth >> subresource.MipLevel) ? (desc.Depth >> subresource.MipLevel) : 1;
		state.Device->GetCopyableFootprints(&nativeDesc, subresourceIndex, 1, 0, &footprint, &rowCount, &rowByteCount, &stagingBytes);
		const FormatInfo& formatInfo = GetFormatInfo(GetTextureTransferFormat(desc.Format, subresource.Aspect));

		if (footprint.Footprint.Depth != depth ||
			rowCount != (height + formatInfo.BlockSize - 1) / formatInfo.BlockSize ||
			rowByteCount != static_cast<uint64>((width + formatInfo.BlockSize - 1) / formatInfo.BlockSize) * formatInfo.BytesPerBlock)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Texture", "The D3D12 texture footprint is incompatible."));
		}

		ComPtr<ID3D12Resource> staging;

		if (!CreateResource(state, stagingBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, staging))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Staging", "Could not create texture readback storage."));
		}

		const EQueue queue = GetFormatInfo(desc.Format).bHasDepth ? EQueue::Graphics : EQueue::Copy;
		Result<Pending> started = Begin(state, queue);

		if (!started)
		{
			return Result<void>::Failure(started.GetError());
		}

		Pending pending = Move(started).TakeValue();

		if (!pending.Temporary.Add(staging))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track texture readback storage."));
		}

		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = texture.Resource.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		source.SubresourceIndex = subresourceIndex;
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = staging.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		destination.PlacedFootprint = footprint;
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE, subresourceIndex);
		pending.List->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON, subresourceIndex);
		Result<uint64> submitted = Execute(state, Move(pending));

		if (!submitted)
		{
			return Result<void>::Failure(submitted.GetError());
		}

		RecordResourceUse(texture, queue, submitted.Value());
		if (texture.bUnknownAliases)
		{
			state.UnknownResourceUses[static_cast<uint32>(queue)] = submitted.Value();
		}

		Result<void> waited = Wait(&state, queue, submitted.Value(), timeout);

		if (!waited)
		{
			return waited;
		}

		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(stagingBytes) };
		if (FAILED(staging->Map(0, &readRange, &mapped)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Map", "Could not map texture readback storage."));
		}

		for (UINT slice = 0; slice < depth; ++slice)
		{
			for (UINT row = 0; row < rowCount; ++row)
			{
				const usize rowIndex = static_cast<usize>(slice) * rowCount + row;
				CopyBytes(output.Data() + rowIndex * rowByteCount,
					static_cast<const Byte*>(mapped) + rowIndex * footprint.Footprint.RowPitch,
					static_cast<usize>(rowByteCount));
			}
		}

		D3D12_RANGE writtenRange{ 0, 0 };
		staging->Unmap(0, &writtenRange);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureCopy(void*, void* native, const NativeTextureCopy& copy)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& source = *static_cast<Texture*>(copy.Source);
		Texture& destination = *static_cast<Texture*>(copy.Destination);
		const UINT sourceSubresource =
			copy.bRegion
			? copy.SourceSubresource + (copy.Aspect == ETextureAspect::Stencil
				? source.Resource->GetDesc().MipLevels * source.Resource->GetDesc().DepthOrArraySize
				: 0)
			: D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		const UINT destinationSubresource =
			copy.bRegion ? copy.DestinationSubresource +
			(copy.Aspect == ETextureAspect::Stencil
				? destination.Resource->GetDesc().MipLevels * destination.Resource->GetDesc().DepthOrArraySize
				: 0)
			: D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE, sourceSubresource);
		Transition(pending, *destination.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST,
			destinationSubresource);

		if (!copy.bRegion)
		{
			pending.List->CopyResource(destination.Resource.Get(), source.Resource.Get());
		}
		else
		{
			D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
			sourceLocation.pResource = source.Resource.Get();
			sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			sourceLocation.SubresourceIndex = sourceSubresource;
			D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
			destinationLocation.pResource = destination.Resource.Get();
			destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			destinationLocation.SubresourceIndex = destinationSubresource;
			D3D12_BOX box{
				copy.SourceX, copy.SourceY, copy.SourceZ, copy.SourceX + copy.Width, copy.SourceY + copy.Height, copy.SourceZ + copy.Depth };
			pending.List->CopyTextureRegion(
				&destinationLocation, copy.DestinationX, copy.DestinationY, copy.DestinationZ, &sourceLocation,
				(GetFormatInfo(source.Format).bHasDepth || source.Resource->GetDesc().SampleDesc.Count != 1) ? nullptr : &box);
		}

		Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON, sourceSubresource);
		Transition(pending, *destination.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON,
			destinationSubresource);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureWrite(
		State* opaque,
		void* native,
		void* destinationNative,
		const TextureDesc& desc,
		TextureSubresource subresource,
		Span<const Byte> data)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(destinationNative);
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT rowCount = 0;
		UINT64 rowByteCount = 0;
		UINT64 stagingBytes = 0;
		const D3D12_RESOURCE_DESC nativeDesc = texture.Resource->GetDesc();
		const UINT subresourceIndex = subresource.ArrayLayer * desc.MipLevels + subresource.MipLevel + (subresource.Aspect == ETextureAspect::Stencil ? desc.MipLevels * desc.ArrayLayers : 0);
		const uint32 width = (desc.Width >> subresource.MipLevel) ? (desc.Width >> subresource.MipLevel) : 1;
		const uint32 height = (desc.Height >> subresource.MipLevel) ? (desc.Height >> subresource.MipLevel) : 1;
		const uint32 depth = (desc.Depth >> subresource.MipLevel) ? (desc.Depth >> subresource.MipLevel) : 1;
		state.Device->GetCopyableFootprints(&nativeDesc, subresourceIndex, 1, 0, &footprint, &rowCount, &rowByteCount, &stagingBytes);
		const FormatInfo& format = GetFormatInfo(GetTextureTransferFormat(desc.Format, subresource.Aspect));

		if (stagingBytes == 0 ||
			footprint.Footprint.Depth != depth ||
			rowCount != (height + format.BlockSize - 1) / format.BlockSize ||
			rowByteCount != static_cast<uint64>((width + format.BlockSize - 1) / format.BlockSize) * format.BytesPerBlock)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Texture", "The texture write footprint is incompatible."));
		}
		ComPtr<ID3D12Resource> staging;

		if (!CreateResource(state, stagingBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, staging))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Staging", "Could not create texture write staging."));
		}
		void* mapped = nullptr;

		if (FAILED(staging->Map(0, nullptr, &mapped)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Map", "Could not map texture write staging."));
		}

		for (UINT slice = 0; slice < depth; ++slice)
		{
			for (UINT row = 0; row < rowCount; ++row)
			{
				const usize rowIndex = static_cast<usize>(slice) * rowCount + row;
				CopyBytes(static_cast<Byte*>(mapped) + rowIndex * footprint.Footprint.RowPitch,
					data.Data() + rowIndex * rowByteCount, static_cast<usize>(rowByteCount));
			}
		}
		staging->Unmap(0, nullptr);

		if (!pending.Temporary.Add(staging))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain texture write staging."));
		}
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = staging.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = texture.Resource.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = subresourceIndex;
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST, subresourceIndex);
		pending.List->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON, subresourceIndex);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<TextureBufferFootprint> GetTextureBufferFootprint(State* opaque, const TextureDesc& desc, TextureSubresource subresource)
	{
		State& state = *opaque;
		const DxgiFormatMapping& mapping = GetDxgiFormatMapping(desc.Format);
		const D3D12_RESOURCE_DESC nativeDesc = MakeTextureResourceDesc( desc, GetFormatInfo(desc.Format).bHasDepth || desc.bMutableFormat ? mapping.ResourceFormat : mapping.SrvFormat);
		const UINT index = subresource.MipLevel + subresource.ArrayLayer * desc.MipLevels + (subresource.Aspect == ETextureAspect::Stencil ? desc.MipLevels * desc.ArrayLayers : 0);
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed{};
		UINT rows = 0;
		UINT64 rowBytes = 0;
		UINT64 bytes = 0;
		state.Device->GetCopyableFootprints(&nativeDesc, index, 1, 0, &placed, &rows, &rowBytes, &bytes);

		if (bytes == UINT64_MAX || rows == 0 || placed.Footprint.Depth == 0 || placed.Footprint.RowPitch == 0)
		{
			return Result<TextureBufferFootprint>::Failure(Failure("Rhi.D3D12Texture", "Could not query the native texture footprint."));
		}

		const D3D12_SUBRESOURCE_FOOTPRINT& footprint = placed.Footprint;

		return Result<TextureBufferFootprint>::Success({ static_cast<uint64>(footprint.RowPitch) * rows * footprint.Depth,
															  footprint.RowPitch, rows, footprint.Width, footprint.Height,
															  footprint.Depth });
	}

	[[nodiscard]] Result<void> PrepareTextureBufferCopy(State* opaque, NativeTextureBufferCopy& copy)
	{
		State& state = *opaque;
		Texture& texture = *static_cast<Texture*>(copy.Texture);
		const D3D12_RESOURCE_DESC desc = texture.Resource->GetDesc();
		const UINT subresource = copy.Subresource.MipLevel + copy.Subresource.ArrayLayer * desc.MipLevels +
			(copy.Subresource.Aspect == ETextureAspect::Stencil ? desc.MipLevels * desc.DepthOrArraySize : 0);
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed{};
		UINT rows = 0;
		UINT64 rowBytes = 0;
		UINT64 bytes = 0;
		state.Device->GetCopyableFootprints(&desc, subresource, 1, 0, &placed, &rows, &rowBytes, &bytes);

		if (bytes == UINT64_MAX ||
			placed.Offset != 0 ||
			placed.Footprint.RowPitch != copy.Footprint.RowPitchBytes ||
			placed.Footprint.Width != copy.Footprint.Width ||
			placed.Footprint.Height != copy.Footprint.Height ||
			placed.Footprint.Depth != copy.Footprint.Depth ||
			rows != copy.Footprint.RowCount ||
			bytes > copy.Footprint.ByteCount)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Texture", "The captured texture buffer footprint is incompatible."));
		}

		copy.NativeFormat = static_cast<uint32>(placed.Footprint.Format);
		copy.NativeSubresource = subresource;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureBuffer(State* opaque, void* native, const NativeTextureBufferCopy& copy, bool bUpload)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(copy.Buffer);
		Texture& texture = *static_cast<Texture*>(copy.Texture);
		NativeTextureBufferCopy captured = copy;
		if (!copy.bPrepared)
		{
			Result<void> prepared = PrepareTextureBufferCopy(opaque, captured);
			if (!prepared)
			{
				return prepared;
			}
		}

		D3D12_TEXTURE_COPY_LOCATION bufferLocation{};
		bufferLocation.pResource = buffer.Resource.Get();
		bufferLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		bufferLocation.PlacedFootprint.Offset = captured.BufferOffsetBytes;
		bufferLocation.PlacedFootprint.Footprint.Format = static_cast<DXGI_FORMAT>(captured.NativeFormat);
		bufferLocation.PlacedFootprint.Footprint.Width = captured.Footprint.Width;
		bufferLocation.PlacedFootprint.Footprint.Height = captured.Footprint.Height;
		bufferLocation.PlacedFootprint.Footprint.Depth = captured.Footprint.Depth;
		bufferLocation.PlacedFootprint.Footprint.RowPitch = captured.Footprint.RowPitchBytes;
		D3D12_TEXTURE_COPY_LOCATION textureLocation{};
		textureLocation.pResource = texture.Resource.Get();
		textureLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		textureLocation.SubresourceIndex = captured.NativeSubresource;
		const D3D12_RESOURCE_STATES bufferState = bUpload ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COPY_DEST;
		const D3D12_RESOURCE_STATES textureState = bUpload ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COPY_SOURCE;

		if (buffer.Memory == EBufferMemory::Device)
		{
			Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, bufferState);
		}
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, textureState, captured.NativeSubresource);
		if (bUpload)
		{
			pending.List->CopyTextureRegion(&textureLocation, 0, 0, 0, &bufferLocation, nullptr);
		}
		else
		{
			pending.List->CopyTextureRegion(&bufferLocation, 0, 0, 0, &textureLocation, nullptr);
		}

		if (!pending.bPrepared)
		{
			Transition(pending, *texture.Resource.Get(), textureState, D3D12_RESOURCE_STATE_COMMON, captured.NativeSubresource);
			if (buffer.Memory == EBufferMemory::Device)
			{
				Transition(pending, *buffer.Resource.Get(), bufferState, D3D12_RESOURCE_STATE_COMMON);
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferToTexture(State* opaque, void* native, const NativeTextureBufferCopy& copy)
	{
		return RecordTextureBuffer(opaque, native, copy, true);
	}

	[[nodiscard]] Result<void> RecordTextureToBuffer(State* opaque, void* native, const NativeTextureBufferCopy& copy)
	{
		return RecordTextureBuffer(opaque, native, copy, false);
	}

	[[nodiscard]] Result<void> RecordTextureResolve(State* opaque, void* native, const NativeTextureResolve& resolve)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& source = *static_cast<Texture*>(resolve.Source);
		Texture& destination = *static_cast<Texture*>(resolve.Destination);
		const D3D12_RESOURCE_DESC srcDesc = source.Resource->GetDesc();
		const D3D12_RESOURCE_DESC dstDesc = destination.Resource->GetDesc();
		const FormatInfo& info = GetFormatInfo(resolve.Format);
		const bool bRegion = info.bHasDepth || resolve.Mode != ETextureResolveMode::Average;
		if (bRegion && !pending.ResolveList)
		{
			if (pending.bPrepared)
			{
				Error error;
				error.Code = EErrorCode::Unsupported;

				return Result<void>::Failure(Move(error));
			}
			NativeTextureResolve copy = resolve;
			Result<void> captured = PrepareTextureResolve(opaque, native, copy);
			if (!captured)
			{
				return captured;
			}
		}

		for (uint32 plane = 0; plane < (info.bHasStencil ? 2u : 1u); ++plane)
		{
			const ETextureResolveMode mode = plane == 0 ? resolve.Mode : resolve.StencilMode;
			if (mode == ETextureResolveMode::None)
			{
				continue;
			}
			const UINT srcIndex = resolve.SourceSubresource.MipLevel + resolve.SourceSubresource.ArrayLayer * srcDesc.MipLevels + plane * srcDesc.MipLevels * srcDesc.DepthOrArraySize;
			const UINT dstIndex = resolve.DestinationSubresource.MipLevel + resolve.DestinationSubresource.ArrayLayer * dstDesc.MipLevels + plane * dstDesc.MipLevels * dstDesc.DepthOrArraySize;
			Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, srcIndex);
			Transition(pending, *destination.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_DEST, dstIndex);
			if (bRegion)
			{
				const D3D12_RESOLVE_MODE nativeMode = mode == ETextureResolveMode::Minimum ? D3D12_RESOLVE_MODE_MIN
					: mode == ETextureResolveMode::Maximum ? D3D12_RESOLVE_MODE_MAX
					: D3D12_RESOLVE_MODE_AVERAGE;
				pending.ResolveList->ResolveSubresourceRegion(
					destination.Resource.Get(), dstIndex, 0, 0, source.Resource.Get(), srcIndex, nullptr,
					plane == 0
					? (info.bHasDepth ? GetDxgiFormatMapping(resolve.Format).SrvFormat : GetDxgiFormatMapping(resolve.Format).RtvFormat)
					: (resolve.Format == EFormat::D24S8 ? DXGI_FORMAT_X24_TYPELESS_G8_UINT : DXGI_FORMAT_X32_TYPELESS_G8X24_UINT),
					nativeMode);
			}
			else
			{
				pending.List->ResolveSubresource(destination.Resource.Get(), dstIndex, source.Resource.Get(), srcIndex,
					GetDxgiFormatMapping(resolve.Format).RtvFormat);
			}

			if (!pending.bPrepared)
			{
				Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_COMMON, srcIndex);
				Transition(pending, *destination.Resource.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COMMON, dstIndex);
			}
		}

		return Result<void>::Success();
	}
}
#endif
