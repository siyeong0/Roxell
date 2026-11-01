#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] Result<void> AppendMicromapUsageCounts(Span<const OpacityMicromapUsageCount> source, Array<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT>& destination)
	{
		if (!destination.Reserve(destination.Size() + source.Size()))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 micromap usage counts."));
		}

		for (const OpacityMicromapUsageCount& count : source)
		{
			if (count.SubdivisionLevel > NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_OC1_MAX_SUBDIVISION_LEVEL)
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12OpacityMicromap", "The micromap subdivision exceeds the device limit.");
			}
			destination.AddRequired(
				{ count.Count, count.SubdivisionLevel, static_cast<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT>(count.Format) });
		}

		return Result<void>::Success();
	}
#endif
#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BUILD_FLAGS MicromapFlags(EOpacityMicromapBuildFlags flags) noexcept
	{
		// NVAPI arrays can be used without a separate compaction build permission.
		return static_cast<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BUILD_FLAGS>(static_cast<uint8>(flags) & 3u);
	}
#endif
#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] bool HasOpacityMicromaps(Span<const NativeAccelTriangleGeometry> triangles) noexcept
	{
		for (const NativeAccelTriangleGeometry& triangle : triangles)
		{
			if (triangle.OpacityMicromap != nullptr)
			{
				return true;
			}
		}

		return false;
	}
#endif
#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] Result<void> ConvertMicromapGeometries(
		Span<const D3D12_RAYTRACING_GEOMETRY_DESC> source,
		Span<const NativeAccelTriangleGeometry> triangles,
		Array<NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX>& destination,
		Array<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT>& counts)
	{
		usize totalCounts = 0;

		for (const NativeAccelTriangleGeometry& triangle : triangles)
		{
			totalCounts += triangle.OpacityMicromapUsageCounts.Size();
		}

		if (!destination.Reserve(source.Size()) || !counts.Reserve(totalCounts))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 micromap geometries."));
		}

		for (usize index = 0; index < source.Size(); ++index)
		{
			NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX geometry{};
			geometry.flags = source[index].Flags;

			if (index >= triangles.Size())
			{
				geometry.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS_EX;
				geometry.aabbs = source[index].AABBs;
			}
			else if (triangles[index].OpacityMicromap == nullptr)
			{
				geometry.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES_EX;
				geometry.triangles = source[index].Triangles;
			}
			else
			{
				const NativeAccelTriangleGeometry& triangle = triangles[index];
				geometry.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES_EX;
				geometry.ommTriangles.triangles = source[index].Triangles;
				using MicromapAttachment = decltype(geometry.ommTriangles.ommAttachment);
				MicromapAttachment& attachment = geometry.ommTriangles.ommAttachment;
				attachment.opacityMicromapArray = static_cast<const NativeMicromap*>(triangle.OpacityMicromap)->Address;
				attachment.opacityMicromapBaseLocation = triangle.OpacityMicromapBaseTriangle;
				attachment.opacityMicromapIndexFormat = triangle.OpacityMicromapIndexFormat == EFormat::R16_UINT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

				if (triangle.OpacityMicromapIndexBuffer != nullptr)
				{
					attachment.opacityMicromapIndexBuffer.StartAddress =
						static_cast<const Buffer*>(triangle.OpacityMicromapIndexBuffer)->Resource->GetGPUVirtualAddress() +
						triangle.OpacityMicromapIndexOffsetBytes;
					attachment.opacityMicromapIndexBuffer.StrideInBytes = triangle.OpacityMicromapIndexStrideBytes;
				}

				const usize countOffset = counts.Size();
				Result<void> copied = AppendMicromapUsageCounts(triangle.OpacityMicromapUsageCounts, counts);

				if (!copied)
				{
					return copied;
				}
				attachment.numOMMUsageCounts = static_cast<NvU32>(triangle.OpacityMicromapUsageCounts.Size());
				attachment.pOMMUsageCounts = counts.Data() + countOffset;
			}
			destination.AddRequired(geometry);
		}

		return Result<void>::Success();
	}
#endif
#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX MicromapAccelInputs(
		const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& source,
		const NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX* geometries) noexcept
	{
		NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX inputs{};
		inputs.type = source.Type;
		inputs.flags = static_cast<NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS_EX>(source.Flags);
		inputs.numDescs = source.NumDescs;
		inputs.descsLayout = source.DescsLayout;
		inputs.geometryDescStrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX);
		inputs.pGeometryDescs = geometries;

		return inputs;
	}
#endif
	[[nodiscard]] Result<OpacityMicromapBuildSizes> GetOpacityMicromapBuildSizes(
		[[maybe_unused]] const State* opaque,
		[[maybe_unused]] Span<const OpacityMicromapUsageCount> counts,
		[[maybe_unused]] EOpacityMicromapBuildFlags flags)
	{
#if ROXELL_RHI_WITH_NVAPI
		const State& state = *opaque;

		if (state.bOpacityMicromap)
		{
			Array<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT> nativeCounts(state.Resource);
			Result<void> copied = AppendMicromapUsageCounts(counts, nativeCounts);

			if (!copied)
			{
				return Result<OpacityMicromapBuildSizes>::Failure(copied.GetError());
			}
			NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_INPUTS inputs{};
			inputs.flags = MicromapFlags(flags);
			inputs.numOMMUsageCounts = static_cast<NvU32>(nativeCounts.Size());
			inputs.pOMMUsageCounts = nativeCounts.Data();
			NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO info{};
			NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS parameters{};
			parameters.version = NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS_VER;
			parameters.pDesc = &inputs;
			parameters.pInfo = &info;

			if (NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo(state.RayDevice.Get(), &parameters) != NVAPI_OK || info.resultDataMaxSizeInBytes == 0 || info.scratchDataSizeInBytes == 0)
			{
				return Result<OpacityMicromapBuildSizes>::Failure(Failure("Rhi.D3D12OpacityMicromap", "Could not query D3D12 micromap build sizes."));
			}

			return Result<OpacityMicromapBuildSizes>::Success({ info.resultDataMaxSizeInBytes, info.scratchDataSizeInBytes, 256, 256 });
		}
#endif
		return Result<OpacityMicromapBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.D3D12OpacityMicromap", "The device has no opacity micromap support.");
	}

	[[nodiscard]] Result<void*> CreateOpacityMicromap(State* opaque, const OpacityMicromapDesc& desc, void* nativeStorage)
	{
		State& state = *opaque;

		if (!state.bOpacityMicromap)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12OpacityMicromap", "The device has no opacity micromap support.");
		}

		d3d12::NativeMicromap* native = CreateObject<NativeMicromap>(state.Resource);

		if (native == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a D3D12 micromap record."));
		}
		native->Storage = static_cast<Buffer*>(nativeStorage)->Resource;
		native->Address = native->Storage->GetGPUVirtualAddress() + desc.ByteOffset;

		return Result<void*>::Success(native);
	}

	void DestroyOpacityMicromapNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<NativeMicromap*>(native));
	}

	[[nodiscard]] Result<void> DestroyOpacityMicromap(State* opaque, void* native, void*)
	{
		DestroyOpacityMicromapNow(opaque, native);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBuildOpacityMicromap(
		[[maybe_unused]] State* opaque,
		[[maybe_unused]] void* nativeCommand,
		[[maybe_unused]] void* nativeMicromap,
		[[maybe_unused]] const OpacityMicromapBuildDesc& desc,
		[[maybe_unused]] void* nativeInput,
		[[maybe_unused]] uint64 inputOffset,
		[[maybe_unused]] void* nativeTriangles,
		[[maybe_unused]] uint64 triangleOffset,
		[[maybe_unused]] void* nativeScratch,
		[[maybe_unused]] uint64 scratchOffset)
	{
#if ROXELL_RHI_WITH_NVAPI
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeCommand);

		if (state.bOpacityMicromap && pending.Queue != EQueue::Copy)
		{
			if (!pending.RayList && FAILED(pending.List.As(&pending.RayList)))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12OpacityMicromap", "The command list does not expose micromap build commands."));
			}

			Array<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT> counts(state.Resource);
			Result<void> copied = AppendMicromapUsageCounts(desc.Counts, counts);

			if (!copied)
			{
				return copied;
			}

			Buffer& input = *static_cast<Buffer*>(nativeInput);
			Buffer& triangles = *static_cast<Buffer*>(nativeTriangles);
			Buffer& scratch = *static_cast<Buffer*>(nativeScratch);
			NativeMicromap& micromap = *static_cast<NativeMicromap*>(nativeMicromap);
			Transition(pending, *input.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			Transition(pending, *triangles.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			Transition(pending, *scratch.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			if (pending.bInvalidResourceState)
			{
				return Result<void>::Failure(Failure("Rhi.D3D12OpacityMicromap", "Could not track micromap build states."));
			}
			NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC build{};
			build.destOpacityMicromapArrayData = micromap.Address;
			build.scratchOpacityMicromapArrayData = scratch.Resource->GetGPUVirtualAddress() + scratchOffset;
			build.inputs.flags = MicromapFlags(desc.Flags);
			build.inputs.numOMMUsageCounts = static_cast<NvU32>(counts.Size());
			build.inputs.pOMMUsageCounts = counts.Data();
			build.inputs.inputBuffer = input.Resource->GetGPUVirtualAddress() + inputOffset;
			build.inputs.perOMMDescs = { triangles.Resource->GetGPUVirtualAddress() + triangleOffset,
										sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC) };
			NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS parameters{};
			parameters.version = NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS_VER;
			parameters.pDesc = &build;
			const NvAPI_Status result = NvAPI_D3D12_BuildRaytracingOpacityMicromapArray(pending.RayList.Get(), &parameters);

			if (result != NVAPI_OK)
			{
				return Result<void>::Failure(Failure("Rhi.D3D12OpacityMicromap", "Could not record the micromap build."));
			}
			EmitUavBarrier(pending, *micromap.Storage.Get());
			Transition(pending, *input.Resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
			Transition(pending, *triangles.Resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
			Transition(pending, *scratch.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

			if (pending.bInvalidResourceState)
			{
				return Result<void>::Failure(Failure("Rhi.D3D12OpacityMicromap", "Could not restore micromap build states."));
			}

			return Result<void>::Success();
		}
#endif
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12OpacityMicromap", "This command list cannot build opacity micromaps.");
	}
}
#endif
