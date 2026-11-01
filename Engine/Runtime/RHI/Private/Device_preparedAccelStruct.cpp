#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] AccelStructBuildDesc Device::preparedBuildDescription(const PreparedRecordingAccelBuild& build) noexcept
	{
		AccelStructBuildDesc description = build.Description.Build;
		description.Triangles = build.Triangles.View();
		description.Aabbs = build.Aabbs.View();

		return description;
	}

	[[nodiscard]] Span<const uint64> Device::preparedAccelShape(const PreparedRecordingPlannedAccel& planned) const noexcept
	{
		return planned.Build != nullptr ? planned.Build->Shape.View() : m_State->m_AccelStructs[planned.ShapeOwner.Slot].PreparedBuildShape.View();
	}

	[[nodiscard]] Span<const AccelStructHandle> Device::preparedAccelReferences(const PreparedRecordingPlannedAccel& planned) const noexcept
	{
		return planned.Build != nullptr ? planned.Build->ReferencedBlas.View()
			: m_State->m_AccelStructs[planned.ShapeOwner.Slot].BottomReferences.View();
	}

	[[nodiscard]] bool Device::preparedAccelContains(const PreparedRecordingPhase& phase, AccelStructHandle handle, EAccelStructState state) noexcept
	{
		for (rhi::PreparedAccelStructAccessDesc const& access : phase.AccelStructs)
		{
			if (access.AccelStruct == handle && access.State == state)
			{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] bool Device::preparedAccelStorageDeclared(const PreparedRecordingPhase& phase, BufferHandle buffer) const noexcept
	{
		for (rhi::PreparedAccelStructAccessDesc const& access : phase.AccelStructs)
		{
			if (m_State->m_AccelStructs[access.AccelStruct.Slot].Storage == buffer)
			{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] bool Device::preparedAccelStorageWrites(const PreparedRecordingPhase& phase, BufferHandle buffer) const noexcept
	{
		for (rhi::PreparedAccelStructAccessDesc const& access : phase.AccelStructs)
		{
			if (m_State->m_AccelStructs[access.AccelStruct.Slot].Storage == buffer && (access.State == EAccelStructState::BuildWrite || access.State == EAccelStructState::CopyDestination))
			{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] bool Device::preparedShapeEqual(Span<const uint64> left, Span<const uint64> right) noexcept
	{
		if (left.Size() != right.Size())
		{
			return false;
		}
		for (usize index = 0; index < left.Size(); ++index)
		{
			if (left[index] != right[index])
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] Result<void> Device::capturePreparedBuildShape(PreparedRecordingAccelBuild& build)
	{
		const AccelStructBuildDesc desc = preparedBuildDescription(build);
		if (!build.Shape.Reserve(6 + desc.Triangles.Size() * 6 + desc.Aabbs.Size() * 3))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}
		const auto add = [&](uint64 value) { (void)build.Shape.Add(value); };
		add(static_cast<uint64>(desc.Type));
		add(desc.bAllowUpdate);
		add(desc.bAllowCompaction);
		add(desc.InstanceCount);
		add(desc.Triangles.Size());
		add(desc.Aabbs.Size());
		for (const rhi::AccelTriangleGeometryDesc& triangle : desc.Triangles)
		{
			add(triangle.VertexStrideBytes);
			add(triangle.VertexCount);
			add(triangle.IndexBuffer.IsValid());
			add(triangle.IndexCount);
			add(static_cast<uint64>(triangle.IndexFormat));
			add(triangle.bOpaque);
		}
		for (const rhi::AccelAabbGeometryDesc& aabb : desc.Aabbs)
		{
			add(aabb.StrideBytes);
			add(aabb.Count);
			add(aabb.bOpaque);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::validatePreparedBuild(PreparedRecordingGroup& group, uint32 listIndex, uint32 phaseIndex, PreparedRecordingAccelBuild& build)
	{
		rhi::PreparedRecordingPhase& phase = group.Lists[listIndex].Phases[phaseIndex];
		const PreparedAccelStructBuildDesc& tuple = build.Description;
		AccelStructSlot* target = findAccelStruct(tuple.Target);
		BufferSlot* scratch = findBuffer(tuple.Scratch);
		const AccelStructBuildDesc desc = preparedBuildDescription(build);
		ASSERT(target != nullptr &&
			target->Type == desc.Type &&
			scratch != nullptr &&
			!scratch->bMapped &&
			scratch->Memory == EBufferMemory::Device &&
			scratch->PermanentState == ETrackedResourceState::Unknown &&
			HasUsage(scratch->Usage, EBufferUsage::Storage) &&
			HasUsage(scratch->Usage, EBufferUsage::DeviceAddress) &&
			tuple.Scratch != target->Storage &&
			preparedAccelContains(phase, tuple.Target, EAccelStructState::BuildWrite),
			"Prepared command arguments must match their declared resources, ranges, and execution order.");
		if (target->TableClosureRefs != 0)
		{
			return preparedFailure(EErrorCode::Conflict);
		}

		for (rhi::PreparedRecordingAccelBuild const& other : phase.AccelBuilds)
		{
			ASSERT(&other == &build || (other.Description.Target != tuple.Target && other.Description.Scratch != tuple.Scratch),
				"Resource access, aliases, and dependencies must match the prepared command declaration.");
		}
		const GpuCapabilities& caps = m_State->m_Backend.Capabilities;
		if (desc.Triangles.Size() + desc.Aabbs.Size() > caps.Limits.MaximumAccelGeometryCount || desc.InstanceCount > caps.Limits.MaximumAccelInstanceCount)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		uint64 primitives = 0;
		for (const rhi::AccelTriangleGeometryDesc& triangle : desc.Triangles)
		{
			primitives += triangle.IndexBuffer.IsValid() ? triangle.IndexCount / 3 : triangle.VertexCount / 3;
			if (triangle.OpacityMicromap.IsValid() ||
				triangle.OpacityMicromapIndexBuffer.IsValid() ||
				!triangle.OpacityMicromapUsageCounts.IsEmpty() ||
				triangle.OpacityMicromapIndexOffsetBytes != 0 ||
				triangle.OpacityMicromapIndexStrideBytes != 0 ||
				triangle.OpacityMicromapBaseTriangle != 0)
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
		}
		for (const rhi::AccelAabbGeometryDesc& aabb : desc.Aabbs)
		{
			primitives += aabb.Count;
		}
		if (primitives > caps.Limits.MaximumAccelPrimitiveCount)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		const auto inputValid = [&](BufferHandle handle) noexcept
			{
				const BufferSlot* buffer = findBuffer(handle);

				return buffer != nullptr &&
					!buffer->bMapped &&
					buffer->Memory == EBufferMemory::Device &&
					HasUsage(buffer->Usage, EBufferUsage::AccelBuildInput) &&
					HasUsage(buffer->Usage, EBufferUsage::DeviceAddress);
			};
		for (rhi::AccelTriangleGeometryDesc const& triangle : build.Triangles)
		{
			ASSERT(inputValid(triangle.VertexBuffer) && (!triangle.IndexBuffer.IsValid() || inputValid(triangle.IndexBuffer)),
				"Prepared command arguments must match their declared resources, ranges, and execution order.");
		}
		for (rhi::AccelAabbGeometryDesc const& aabb : build.Aabbs)
		{
			ASSERT(inputValid(aabb.Buffer), "Prepared command arguments must match their declared resources, ranges, and execution order.");
		}

		Result<AccelStructBuildSizes> sizes = getAccelStructBuildSizes(desc);
		if (!sizes)
		{
			return preparedFailure(sizes.GetError().Code);
		}

		const uint64 scratchBytes = desc.bPerformUpdate ? sizes.Value().UpdateScratchBytes : sizes.Value().BuildScratchBytes;
		ASSERT(scratchBytes != 0 &&
			(desc.bPerformUpdate || target->ByteCount >= sizes.Value().ResultBytes) &&
			tuple.ScratchOffsetBytes % sizes.Value().ScratchAlignmentBytes == 0 &&
			Fits(scratch->ByteCount, tuple.ScratchOffsetBytes, scratchBytes) &&
			preparedBufferContains(phase, tuple.Scratch, EBufferState::AccelScratch, tuple.ScratchOffsetBytes, scratchBytes),
			"Prepared command arguments must match their declared resources, ranges, and execution order.");

		Result<void> shape = capturePreparedBuildShape(build);
		if (!shape)
		{
			return shape;
		}
		rhi::PreparedRecordingPlannedAccel& planned = group.PlannedAccels[tuple.Target.Slot];
		const uint64 revision = planned.BuildRevision;
		if (revision == UINT64_MAX)
		{
			return preparedFailure(EErrorCode::Conflict);
		}
		build.BuildRevision = revision + 1;
		ASSERT(!desc.bPerformUpdate ||
			(planned.bAvailable &&
					planned.bAllowUpdate &&
					(planned.Build != nullptr || m_State->m_AccelStructs[planned.ShapeOwner.Slot].bPreparedBuildShape) &&
					preparedShapeEqual(preparedAccelShape(planned), build.Shape.View())),
			"Resource access, aliases, and dependencies must match the prepared command declaration.");
		if (!target->PreparedBuildShape.Reserve(build.Shape.Size()) ||
			!target->BottomReferences.Reserve(build.ReferencedBlas.Size()) ||
			!target->PreparedBottomReferences.Reserve(build.ReferencedBlas.Size()) ||
			!build.NativeTriangles.Reserve(build.Triangles.Size()) ||
			!build.NativeAabbs.Reserve(build.Aabbs.Size()))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}
		for (rhi::AccelTriangleGeometryDesc const& triangle : build.Triangles)
		{
			const uint64 vertexBytes = uint64(triangle.VertexCount - 1) * triangle.VertexStrideBytes + 12;
			ASSERT(preparedBufferContains(phase, triangle.VertexBuffer, EBufferState::AccelBuildInput, triangle.VertexOffsetBytes, vertexBytes),
				"Resource access, aliases, and dependencies must match the prepared command declaration.");
			ASSERT(triangle.VertexBuffer != tuple.Scratch &&
				triangle.VertexBuffer != target->Storage &&
				(!triangle.IndexBuffer.IsValid() || (triangle.IndexBuffer != tuple.Scratch && triangle.IndexBuffer != target->Storage)),
				"Resource access, aliases, and dependencies must match the prepared command declaration.");
			void* indices = nullptr;
			if (triangle.IndexBuffer.IsValid())
			{
				const uint64 bytes = uint64(triangle.IndexCount) * (triangle.IndexFormat == EFormat::R16_UINT ? 2u : 4u);
				ASSERT(preparedBufferContains(phase, triangle.IndexBuffer, EBufferState::AccelBuildInput, triangle.IndexOffsetBytes, bytes),
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
				indices = m_State->m_Buffers[triangle.IndexBuffer.Slot].Native;
			}
			(void)build.NativeTriangles.Add({ m_State->m_Buffers[triangle.VertexBuffer.Slot].Native, triangle.VertexOffsetBytes, triangle.VertexStrideBytes,
											 triangle.VertexCount, indices, triangle.IndexOffsetBytes, triangle.IndexCount, triangle.IndexFormat,
											 triangle.bOpaque });
		}
		for (rhi::AccelAabbGeometryDesc const& aabb : build.Aabbs)
		{
			ASSERT(aabb.Buffer != tuple.Scratch &&
				aabb.Buffer != target->Storage &&
				preparedBufferContains(phase, aabb.Buffer, EBufferState::AccelBuildInput, aabb.OffsetBytes, uint64(aabb.Count - 1) * aabb.StrideBytes + 24),
				"Resource access, aliases, and dependencies must match the prepared command declaration.");
			(void)build.NativeAabbs.Add({ m_State->m_Buffers[aabb.Buffer.Slot].Native, aabb.OffsetBytes, aabb.StrideBytes, aabb.Count, aabb.bOpaque });
		}
		if (desc.Type == EAccelStructType::BottomLevel)
		{
			ASSERT(!tuple.Instances.IsValid() && tuple.InstanceOffsetBytes == 0 && build.ReferencedBlas.IsEmpty(),
				"Prepared command arguments must match their declared resources, ranges, and execution order.");
		}
		else
		{
			const BufferSlot* instances = findBuffer(tuple.Instances);
			const uint64 bytes = uint64(desc.InstanceCount) * sizeof(AccelGpuInstanceDesc);
			ASSERT(instances != nullptr &&
				!instances->bMapped &&
				instances->Memory == EBufferMemory::Device &&
				HasUsage(instances->Usage, EBufferUsage::AccelBuildInput) &&
				HasUsage(instances->Usage, EBufferUsage::DeviceAddress) &&
				tuple.Instances != tuple.Scratch &&
				tuple.Instances != target->Storage &&
				tuple.InstanceOffsetBytes % 16 == 0 &&
				Fits(instances->ByteCount, tuple.InstanceOffsetBytes, bytes) &&
				!build.ReferencedBlas.IsEmpty() &&
				preparedBufferContains(phase, tuple.Instances, EBufferState::AccelBuildInput, tuple.InstanceOffsetBytes, bytes),
				"Prepared command arguments must match their declared resources, ranges, and execution order.");
			for (rhi::AccelStructHandle const bottomHandle : build.ReferencedBlas)
			{
				const AccelStructSlot* bottom = findAccelStruct(bottomHandle);
				ASSERT(bottom != nullptr &&
					bottom->Type == EAccelStructType::BottomLevel &&
					bottom->Storage != target->Storage &&
					bottom->Storage != tuple.Scratch &&
					bottom->Storage != tuple.Instances &&
					group.PlannedAccels[bottomHandle.Slot].bAvailable &&
					preparedAccelContains(phase, bottomHandle, EAccelStructState::BuildRead),
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
				rhi::PreparedRecordingPlannedAccel& bottomPlanned = group.PlannedAccels[bottomHandle.Slot];
				if (bottomPlanned.ParentPinCount == UINT32_MAX || bottom->PreparedParentRefs >= UINT32_MAX - bottomPlanned.ParentPinCount)
				{
					return preparedFailure(EErrorCode::Conflict);
				}
				++bottomPlanned.ParentPinCount;
			}
		}
		planned.Build = &build;
		planned.BuildRevision = build.BuildRevision;
		planned.bAvailable = true;
		planned.bAllowUpdate = desc.bAllowUpdate;
		planned.bAllowCompaction = desc.bAllowCompaction;

		return Result<void>::Success();
	}

	[[nodiscard]] bool Device::preparedAccelOperationsComplete(const PreparedRecordingPhase& phase) noexcept
	{
		for (rhi::PreparedRecordingAccelBuild const& build : phase.AccelBuilds)
		{
			if (!build.bEmitted)
			{
				return false;
			}
		}
		for (rhi::PreparedRecordingAccelQuery const& query : phase.AccelCompactionQueries)
		{
			if (!query.bEmitted)
			{
				return false;
			}
		}
		for (rhi::PreparedRecordingAccelCompact const& compact : phase.AccelCompactions)
		{
			if (!compact.bEmitted)
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] Result<void> Device::prepareAccelPhase(PreparedRecordingGroup& group, uint32 listIndex, uint32 phaseIndex)
	{
		rhi::PreparedRecordingPhase& phase = group.Lists[listIndex].Phases[phaseIndex];
		if (phase.AccelStructs.IsEmpty() && phase.AccelBuilds.IsEmpty() && phase.AccelCompactionQueries.IsEmpty() && phase.AccelCompactions.IsEmpty())
		{
			return Result<void>::Success();
		}
		if (group.Lists[listIndex].Queue == EQueue::Copy ||
			!m_State->m_Backend.Capabilities.Features.bAccelerationStructure ||
			backend::HAS_PREPARE_ACCEL_BUILD == false ||
			backend::HAS_PREPARE_ACCEL_QUERY == false ||
			backend::HAS_PREPARE_ACCEL_COMPACT == false ||
			backend::HAS_TRY_GET_ACCEL_COMPACTED_SIZE == false)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		ASSERT(phase.AccelBuilds.Size() <= phase.Capacity.AccelBuilds &&
			phase.AccelCompactionQueries.Size() <= phase.Capacity.AccelCompactionQueries &&
			phase.AccelCompactions.Size() <= phase.Capacity.AccelCompactions,
			"Prepared command arguments must match their declared resources, ranges, and execution order.");
		// Capture transitive BLAS owners before building any borrowed native phase spans.
		usize dependencies = 0;
		for (rhi::PreparedAccelStructAccessDesc const& access : phase.AccelStructs)
		{
			const AccelStructSlot* accel = findAccelStruct(access.AccelStruct);
			ASSERT(accel != nullptr && access.State <= EAccelStructState::CopyDestination, "Prepared command arguments must match their declared resources, ranges, and execution order.");
			if (access.State == EAccelStructState::ShaderRead && accel->Type == EAccelStructType::TopLevel)
			{
				const usize count = preparedAccelReferences(group.PlannedAccels[access.AccelStruct.Slot]).Size();
				if (!AddMemorySize(dependencies, count, &dependencies))
				{
					return preparedFailure(EErrorCode::ExternalFailure);
				}
			}
		}
		usize capacity = 0;
		if (!AddMemorySize(phase.AccelStructs.Size(), dependencies, &capacity) ||
			capacity > UINT32_MAX ||
			!phase.AccelStructs.Reserve(capacity) ||
			!AddMemorySize(phase.Buffers.Size(), capacity, &capacity) ||
			!phase.Buffers.Reserve(capacity))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}
		for (rhi::PreparedBufferAccessDesc& access : phase.Buffers)
		{
			const BufferSlot* buffer = findBuffer(access.Buffer);
			ASSERT(buffer != nullptr && access.Range.ByteOffset <= buffer->ByteCount, "Prepared command arguments must match their declared resources, ranges, and execution order.");
			if (access.Range.ByteCount == 0)
			{
				access.Range.ByteCount = buffer->ByteCount - access.Range.ByteOffset;
			}
		}

		const usize originalCount = phase.AccelStructs.Size();
		for (usize index = 0; index < originalCount; ++index)
		{
			const PreparedAccelStructAccessDesc access = phase.AccelStructs[index];
			rhi::AccelStructSlot const& accel = m_State->m_AccelStructs[access.AccelStruct.Slot];
			if (access.State != EAccelStructState::ShaderRead || accel.Type != EAccelStructType::TopLevel)
			{
				continue;
			}

			const Span<const AccelStructHandle> references = preparedAccelReferences(group.PlannedAccels[access.AccelStruct.Slot]);
			for (const rhi::AccelStructHandle child : references)
			{
				bool bPresent = false;
				for (rhi::PreparedAccelStructAccessDesc const& existing : phase.AccelStructs)
				{
					bPresent = bPresent || existing.AccelStruct == child;
				}
				if (!bPresent)
				{
					(void)phase.AccelStructs.Add({ child, EAccelStructState::ShaderRead });
				}
			}
		}
		for (rhi::PreparedAccelStructAccessDesc const& access : phase.AccelStructs)
		{
			const AccelStructSlot* accel = findAccelStruct(access.AccelStruct);
			const BufferSlot* storage = accel != nullptr ? findBuffer(accel->Storage) : nullptr;
			const bool bWrite = access.State == EAccelStructState::BuildWrite || access.State == EAccelStructState::CopyDestination;
			if (accel == nullptr || storage == nullptr || storage->bMapped || (!bWrite && !group.PlannedAccels[access.AccelStruct.Slot].bAvailable))
			{
				return preparedFailure(EErrorCode::Conflict);
			}
			for (rhi::PreparedAccelStructAccessDesc const& other : phase.AccelStructs)
			{
				const AccelStructSlot* otherAccel = findAccelStruct(other.AccelStruct);
				ASSERT(&access == &other ||
					otherAccel == nullptr ||
					accel->Storage != otherAccel->Storage ||
					(access.AccelStruct != other.AccelStruct && !bWrite && other.State != EAccelStructState::BuildWrite && other.State != EAccelStructState::CopyDestination),
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
			}
			rhi::PreparedRecordingPlannedAccel& planned = group.PlannedAccels[access.AccelStruct.Slot];
			if (planned.PinCount == UINT32_MAX || accel->RecordedUses >= UINT32_MAX - planned.PinCount || accel->PreparedReferences >= UINT32_MAX - planned.PinCount)
			{
				return preparedFailure(EErrorCode::Conflict);
			}
			++planned.PinCount;
			(void)phase.Buffers.Add({ accel->Storage, {accel->ByteOffset, accel->ByteCount}, EBufferState::AccelStorage });
		}
		for (rhi::PreparedRecordingAccelBuild& build : phase.AccelBuilds)
		{
			Result<void> valid = validatePreparedBuild(group, listIndex, phaseIndex, build);
			if (!valid)
			{
				return valid;
			}
		}
		for (rhi::PreparedRecordingAccelQuery& query : phase.AccelCompactionQueries)
		{
			AccelStructSlot* source = findAccelStruct(query.Description.Source);
			rhi::PreparedRecordingPlannedAccel const* planned = source != nullptr ? &group.PlannedAccels[query.Description.Source.Slot] : nullptr;
			if (source == nullptr ||
				source->Type != EAccelStructType::BottomLevel ||
				!planned->bAllowCompaction ||
				!preparedAccelContains(phase, query.Description.Source, EAccelStructState::BuildRead) ||
				source->NextQueryGeneration == UINT64_MAX ||
				source->PreparedReferences != 0 ||
				planned->bQueryPlanned)
			{
				return preparedFailure(EErrorCode::Conflict);
			}
			// Every query is a fresh owner. Outstanding prior native queries must retire before replacement.
			if (source->QueryGeneration != 0)
			{
				Result<AccelCompactionResult> previous = backend::TryGetAccelCompactedSize(m_State->m_Backend.State, source->Native, source->QueryGeneration);
				if (!previous || previous.Value().State == EAccelCompactionQueryState::Pending)
				{
					return preparedFailure(previous ? EErrorCode::Conflict : previous.GetError().Code);
				}
			}
			query.QueryGeneration = source->NextQueryGeneration;
			query.BuildRevision = planned->BuildRevision;
			group.PlannedAccels[query.Description.Source.Slot].bQueryPlanned = true;
		}
		for (rhi::PreparedRecordingAccelCompact& compact : phase.AccelCompactions)
		{
			const PreparedAccelStructCompactDesc& tuple = compact.Description;
			AccelStructSlot* source = findAccelStruct(tuple.Source);
			AccelStructSlot* target = findAccelStruct(tuple.Target);
			if (source == nullptr ||
				target == nullptr ||
				source->Type != EAccelStructType::BottomLevel ||
				target->Type != EAccelStructType::BottomLevel ||
				source->Storage == target->Storage ||
				!source->bBuilt ||
				source->bUnavailable ||
				!source->bAllowCompaction ||
				source->CompactedBytes == 0 ||
				source->CompactedBytes > target->ByteCount ||
				source->bQueryUnavailable ||
				source->QueryBuildRevision != source->BuildRevision ||
				group.PlannedAccels[tuple.Source.Slot].BuildRevision != source->BuildRevision ||
				!preparedAccelContains(phase, tuple.Source, EAccelStructState::CopySource) ||
				!preparedAccelContains(phase, tuple.Target, EAccelStructState::CopyDestination) ||
				group.PlannedAccels[tuple.Target.Slot].BuildRevision == UINT64_MAX)
			{
				return preparedFailure(EErrorCode::Conflict);
			}
			if (!target->PreparedBuildShape.Reserve(preparedAccelShape(group.PlannedAccels[tuple.Source.Slot]).Size()))
			{
				return preparedFailure(EErrorCode::ExternalFailure);
			}
			compact.BuildRevision = group.PlannedAccels[tuple.Target.Slot].BuildRevision + 1;
			const bool bQueryPlanned = group.PlannedAccels[tuple.Target.Slot].bQueryPlanned;
			const uint32 pinCount = group.PlannedAccels[tuple.Target.Slot].PinCount;
			const uint32 parentPinCount = group.PlannedAccels[tuple.Target.Slot].ParentPinCount;
			group.PlannedAccels[tuple.Target.Slot] = group.PlannedAccels[tuple.Source.Slot];
			group.PlannedAccels[tuple.Target.Slot].BuildRevision = compact.BuildRevision;
			group.PlannedAccels[tuple.Target.Slot].bQueryPlanned = bQueryPlanned;
			group.PlannedAccels[tuple.Target.Slot].PinCount = pinCount;
			group.PlannedAccels[tuple.Target.Slot].ParentPinCount = parentPinCount;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::capturePreparedAccelPhase(CommandSlot& command, PreparedRecordingPhase& phase)
	{
		for (rhi::PreparedRecordingAccelBuild& build : phase.AccelBuilds)
		{
			const PreparedAccelStructBuildDesc& tuple = build.Description;
			NativePreparedAccelBuild native;
			native.Target = m_State->m_AccelStructs[tuple.Target.Slot].Native;
			native.Build = preparedBuildDescription(build);
			native.Triangles = build.NativeTriangles.View();
			native.Aabbs = build.NativeAabbs.View();
			native.Scratch = m_State->m_Buffers[tuple.Scratch.Slot].Native;
			native.ScratchOffsetBytes = tuple.ScratchOffsetBytes;
			native.Instances = tuple.Instances.IsValid() ? m_State->m_Buffers[tuple.Instances.Slot].Native : nullptr;
			native.InstanceOffsetBytes = tuple.InstanceOffsetBytes;
			Result<uint32> captured = backend::PrepareAccelBuild(m_State->m_Backend.State, command.Native, native);
			if (!captured)
			{
				return preparedFailure(captured.GetError().Code);
			}
			build.NativeIndex = captured.Value();
		}
		for (rhi::PreparedRecordingAccelQuery& query : phase.AccelCompactionQueries)
		{
			Result<uint32> captured = backend::PrepareAccelQuery( m_State->m_Backend.State, command.Native, m_State->m_AccelStructs[query.Description.Source.Slot].Native, query.QueryGeneration);
			if (!captured)
			{
				return preparedFailure(captured.GetError().Code);
			}
			query.NativeIndex = captured.Value();
		}
		for (rhi::PreparedRecordingAccelCompact& compact : phase.AccelCompactions)
		{
			Result<uint32> captured =
				backend::PrepareAccelCompact(m_State->m_Backend.State, command.Native, m_State->m_AccelStructs[compact.Description.Source.Slot].Native,
					m_State->m_AccelStructs[compact.Description.Target.Slot].Native);
			if (!captured)
			{
				return preparedFailure(captured.GetError().Code);
			}
			compact.NativeIndex = captured.Value();
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<AccelCompactionResult> Device::TryGetBottomLevelAccelStructCompactedSize(AccelStructHandle handle)
	{
		CallScope call(*this, false);
		using R = Result<AccelCompactionResult>;
		AccelStructSlot* source = findAccelStruct(handle);
		ASSERT(source != nullptr && source->Type == EAccelStructType::BottomLevel, "Compaction queries require a live bottom-level acceleration structure.");
		if (!m_State->m_Backend.Capabilities.Features.bAccelerationStructure || backend::HAS_TRY_GET_ACCEL_COMPACTED_SIZE == false)
		{
			Error error;
			error.Code = EErrorCode::Unsupported;

			return R::Failure(Move(error));
		}
		if (!source->bBuilt || source->bUnavailable || source->bQueryUnavailable || source->QueryGeneration == 0 || source->QueryBuildRevision != source->BuildRevision)
		{
			return R::Success({ EAccelCompactionQueryState::Unavailable, 0 });
		}
		Result<AccelCompactionResult> result = backend::TryGetAccelCompactedSize(m_State->m_Backend.State, source->Native, source->QueryGeneration);
		if (result && result.Value().State == EAccelCompactionQueryState::Ready)
		{
			if (result.Value().ByteCount == 0 || result.Value().ByteCount > source->ByteCount)
			{
				source->bQueryUnavailable = true;

				return R::Success({ EAccelCompactionQueryState::Unavailable, 0 });
			}
			source->CompactedBytes = result.Value().ByteCount;
		}

		return result;
	}

	void Device::commitPreparedAccelMetadata(CommandSlot& command, ESubmissionDisposition disposition, uint64 serial) noexcept
	{
		rhi::PreparedRecordingList const& list = command.PreparedGroup->Lists[command.PreparedIndex];
		if (disposition == ESubmissionDisposition::NotAccepted)
		{
			return;
		}
		const bool bUncertain = disposition == ESubmissionDisposition::Indeterminate;
		for (rhi::PreparedRecordingPhase const& phase : list.Phases)
		{
			for (rhi::PreparedRecordingAccelBuild const& build : phase.AccelBuilds)
			{
				rhi::AccelStructSlot& target = m_State->m_AccelStructs[build.Description.Target.Slot];
				target.BuildRevision = build.BuildRevision;
				target.CompactedBytes = 0;
				target.bQueryUnavailable = true;
				target.bBuilt = !bUncertain;
				target.bUnavailable = bUncertain;
				if (!bUncertain)
				{
					target.Signature = makeAccelBuildSignature(preparedBuildDescription(build));
					target.bAllowUpdate = build.Description.Build.bAllowUpdate;
					target.bAllowCompaction = build.Description.Build.bAllowCompaction;
					target.PreparedBuildShape.Clear();
					for (unsigned long long const value : build.Shape)
					{
						(void)target.PreparedBuildShape.Add(value);
					}
					target.bPreparedBuildShape = true;
					for (rhi::AccelStructHandle const handle : target.PreparedBottomReferences)
					{
						--m_State->m_AccelStructs[handle.Slot].PreparedParentRefs;
					}
					target.PreparedBottomReferences.Clear();
					target.BottomReferences.Clear();
					for (rhi::AccelStructHandle const handle : build.ReferencedBlas)
					{
						(void)target.BottomReferences.Add(handle);
						(void)target.PreparedBottomReferences.Add(handle);
						++m_State->m_AccelStructs[handle.Slot].PreparedParentRefs;
					}
				}
			}
			for (rhi::PreparedRecordingAccelCompact const& compact : phase.AccelCompactions)
			{
				rhi::AccelStructSlot const& source = m_State->m_AccelStructs[compact.Description.Source.Slot];
				rhi::AccelStructSlot& target = m_State->m_AccelStructs[compact.Description.Target.Slot];
				target.BuildRevision = compact.BuildRevision;
				target.bBuilt = !bUncertain;
				target.bUnavailable = bUncertain;
				target.bQueryUnavailable = true;
				target.CompactedBytes = 0;
				if (!bUncertain)
				{
					target.Signature = source.Signature;
					target.bAllowUpdate = source.bAllowUpdate;
					target.bAllowCompaction = source.bAllowCompaction;
					target.PreparedBuildShape.Clear();
					for (unsigned long long const value : source.PreparedBuildShape)
					{
						(void)target.PreparedBuildShape.Add(value);
					}
					target.bPreparedBuildShape = source.bPreparedBuildShape;
				}
			}
			for (rhi::PreparedAccelStructAccessDesc const& access : phase.AccelStructs)
			{
				if (bUncertain && (access.State == EAccelStructState::BuildWrite || access.State == EAccelStructState::CopyDestination))
				{
					rhi::AccelStructSlot& target = m_State->m_AccelStructs[access.AccelStruct.Slot];
					target.bBuilt = false;
					target.bUnavailable = true;
					target.bQueryUnavailable = true;
					target.CompactedBytes = 0;
				}
			}
			for (rhi::PreparedRecordingAccelQuery const& query : phase.AccelCompactionQueries)
			{
				rhi::AccelStructSlot& source = m_State->m_AccelStructs[query.Description.Source.Slot];
				backend::CommitPreparedAccelQuery(m_State->m_Backend.State, source.Native, query.QueryGeneration, command.Queue, serial,
					bUncertain);
				source.QueryGeneration = query.QueryGeneration;
				source.QueryBuildRevision = query.BuildRevision;
				source.bQueryUnavailable = bUncertain || source.BuildRevision != query.BuildRevision;
				source.CompactedBytes = 0;
			}
		}
	}

	[[nodiscard]] PreparedAccelStructBuildRef Device::GetPreparedAccelStructBuild(CommandListHandle handle, uint32 phase, uint32 slot) const
	{
		CallScope call(*this, true, true);
		const CommandSlot* command = findCommand(handle);
		rhi::PreparedRecordingList const* list = command != nullptr && command->PreparedGroup ? &command->PreparedGroup->Lists[command->PreparedIndex] : nullptr;
		ASSERT(!(list == nullptr || phase >= list->Phases.Size() || slot >= list->Phases[phase].AccelBuilds.Size()), "Prepared selectors require a valid list, phase, and slot.");

		return PreparedAccelStructBuildRef{ handle, phase, slot };
	}

	[[nodiscard]] Result<void> Device::BuildAccelStructPrepared(CommandListHandle handle, PreparedAccelStructBuildRef ref)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(!(phase == nullptr || ref.List != handle || ref.Phase != preparedList(command)->ActivePhase || ref.Slot >= phase->AccelBuilds.Size() || command->bRendering),
			"Prepared recording requires valid admitted input and capacity.");
		rhi::PreparedRecordingAccelBuild& operation = phase->AccelBuilds[ref.Slot];
		ASSERT(!(operation.bEmitted || phase->Used.AccelBuilds >= phase->Capacity.AccelBuilds), "Prepared recording requires valid admitted input and capacity.");
		Result<void> result =
			recordNative([&] { return backend::RecordPreparedAccelBuild(m_State->m_Backend.State, command->Native, operation.NativeIndex); });
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}
		operation.bEmitted = true;
		++phase->Used.AccelBuilds;

		return Result<void>::Success();
	}

	[[nodiscard]] PreparedAccelStructCompactionQueryRef Device::GetPreparedAccelStructCompactionQuery(CommandListHandle handle, uint32 phase, uint32 slot) const
	{
		CallScope call(*this, true, true);
		const CommandSlot* command = findCommand(handle);
		rhi::PreparedRecordingList const* list = command != nullptr && command->PreparedGroup ? &command->PreparedGroup->Lists[command->PreparedIndex] : nullptr;
		ASSERT(!(list == nullptr || phase >= list->Phases.Size() || slot >= list->Phases[phase].AccelCompactionQueries.Size()), "Prepared selectors require a valid list, phase, and slot.");

		return PreparedAccelStructCompactionQueryRef{ handle, phase, slot };
	}

	[[nodiscard]] Result<void> Device::QueryAccelStructCompactedSizePrepared(CommandListHandle handle, PreparedAccelStructCompactionQueryRef ref)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(!(phase == nullptr || ref.List != handle || ref.Phase != preparedList(command)->ActivePhase || ref.Slot >= phase->AccelCompactionQueries.Size() || command->bRendering),
			"Prepared recording requires valid admitted input and capacity.");
		rhi::PreparedRecordingAccelQuery& operation = phase->AccelCompactionQueries[ref.Slot];
		ASSERT(!(operation.bEmitted || phase->Used.AccelCompactionQueries >= phase->Capacity.AccelCompactionQueries), "Prepared recording requires valid admitted input and capacity.");
		Result<void> result =
			recordNative([&] { return backend::RecordPreparedAccelQuery(m_State->m_Backend.State, command->Native, operation.NativeIndex); });
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}
		operation.bEmitted = true;
		++phase->Used.AccelCompactionQueries;

		return Result<void>::Success();
	}

	[[nodiscard]] PreparedAccelStructCompactRef Device::GetPreparedAccelStructCompact(CommandListHandle handle, uint32 phase, uint32 slot) const
	{
		CallScope call(*this, true, true);
		const CommandSlot* command = findCommand(handle);
		rhi::PreparedRecordingList const* list = command != nullptr && command->PreparedGroup ? &command->PreparedGroup->Lists[command->PreparedIndex] : nullptr;
		ASSERT(!(list == nullptr || phase >= list->Phases.Size() || slot >= list->Phases[phase].AccelCompactions.Size()), "Prepared selectors require a valid list, phase, and slot.");

		return PreparedAccelStructCompactRef{ handle, phase, slot };
	}

	[[nodiscard]] Result<void> Device::CompactAccelStructPrepared(CommandListHandle handle, PreparedAccelStructCompactRef ref)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(!(phase == nullptr || ref.List != handle || ref.Phase != preparedList(command)->ActivePhase || ref.Slot >= phase->AccelCompactions.Size() || command->bRendering),
			"Prepared recording requires valid admitted input and capacity.");
		rhi::PreparedRecordingAccelCompact& operation = phase->AccelCompactions[ref.Slot];
		ASSERT(!(operation.bEmitted || phase->Used.AccelCompactions >= phase->Capacity.AccelCompactions), "Prepared recording requires valid admitted input and capacity.");
		Result<void> result = recordNative(
			[&] { return backend::RecordPreparedAccelCompact(m_State->m_Backend.State, command->Native, operation.NativeIndex); });
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}
		operation.bEmitted = true;
		++phase->Used.AccelCompactions;

		return Result<void>::Success();
	}
}
