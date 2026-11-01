#include "PreparedRecordingGroup.h"
#include "PreparedProgramCache.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <Comparison.hpp>
#include <Trace.h>
#include <Types.h>
#include <Utility.hpp>

namespace rox::rhi
{
	using namespace core;

	PreparedRecordingAccelBuild::PreparedRecordingAccelBuild(MemoryResource& memory) noexcept
		: Triangles(memory)
		, Aabbs(memory)
		, ReferencedBlas(memory)
		, NativeTriangles(memory)
		, NativeAabbs(memory)
		, Shape(memory)
	{
	}

	PreparedRecordingPhase::PreparedRecordingPhase(MemoryResource& memory, SharedPtr<PreparedAccessProgram> program) noexcept
		: AccessProgram(Move(program))
		, Buffers(AccessProgram->Buffers)
		, Textures(AccessProgram->Textures)
		, BufferLookup(AccessProgram->BufferLookup)
		, TextureLookup(AccessProgram->TextureLookup)
		, Bindings(memory)
		, ValidatedFixedSets(AccessProgram->FixedSetCoverage)
		, Attachments(memory)
		, TextureBufferCopies(memory)
		, Resolves(memory)
		, Indirects(memory)
		, AccelStructs(memory)
		, AccelBuilds(memory)
		, AccelCompactionQueries(memory)
		, AccelCompactions(memory)
		, NativeBuffers(AccessProgram->NativeBuffers)
		, NativeTextures(AccessProgram->NativeTextures)
	{
	}

	bool PreparedRecordingPhase::BuildAccessLookup()
	{
		if (AccessProgram->bValidated)
		{
			bBufferLookupReady = true;
			return true;
		}
		ASSERT(BufferLookup.IsEmpty() && TextureLookup.IsEmpty(), "Phase access lookup is built once.");
		if (Buffers.Size() > UINT32_MAX || Textures.Size() > UINT32_MAX || !BufferLookup.Reserve(Buffers.Size()) || !TextureLookup.Reserve(Textures.Size()))
		{
			return false;
		}
		for (uint32 index = 0; index < Buffers.Size(); ++index)
		{
			(void)BufferLookup.Add({ index });
		}
		for (uint32 index = 0; index < Textures.Size(); ++index)
		{
			(void)TextureLookup.Add(index);
		}
		if (!BufferLookup.IsEmpty())
		{
			Sort(BufferLookup.begin(), BufferLookup.end(),
				[&](const BufferLookupEntry& left, const BufferLookupEntry& right) noexcept
				{
					rhi::PreparedBufferAccessDesc const& lhs = Buffers[left.Source];
					rhi::PreparedBufferAccessDesc const& rhs = Buffers[right.Source];

					return lhs.Buffer < rhs.Buffer || (lhs.Buffer == rhs.Buffer && lhs.Range.ByteOffset < rhs.Range.ByteOffset);
				});
			for (usize begin = 0; begin < BufferLookup.Size();)
			{
				rhi::PreparedBufferAccessDesc const& first = Buffers[BufferLookup[begin].Source];
				usize end = begin + 1;
				bool bConflict = false;
				while (end < BufferLookup.Size() && Buffers[BufferLookup[end].Source].Buffer == first.Buffer)
				{
					bConflict |= Buffers[BufferLookup[end].Source].State != first.State;
					++end;
				}
				for (usize index = begin; index < end; ++index)
				{
					BufferLookup[index].bConflictingStates = bConflict;
				}
				begin = end;
			}
		}
		if (!TextureLookup.IsEmpty())
		{
			Sort(TextureLookup.begin(), TextureLookup.end(), [&](uint32 left, uint32 right) noexcept { return Textures[left].Texture < Textures[right].Texture; });
		}

		return true;
	}

	void PreparedRecordingPhase::CompleteBufferLookup() noexcept
	{
		if (AccessProgram->bValidated)
		{
			bBufferLookupReady = true;
			return;
		}
		BufferHandle previous;
		uint64 maximumEnd = 0;
		for (rhi::PreparedAccessProgram::BufferLookupEntry& indexed : BufferLookup)
		{
			rhi::PreparedBufferAccessDesc const& access = Buffers[indexed.Source];
			ASSERT(access.Range.ByteCount <= UINT64_MAX - access.Range.ByteOffset, "Validated phase ranges cannot overflow their end offset.");
			if (access.Buffer != previous)
			{
				maximumEnd = 0;
				previous = access.Buffer;
			}
			maximumEnd = Max(maximumEnd, access.Range.ByteOffset + access.Range.ByteCount);
			indexed.MaximumEnd = maximumEnd;
		}
		bBufferLookupReady = true;
	}

	bool PreparedRecordingPhase::HasBufferStateConflict(BufferHandle buffer) const noexcept
	{
		if (BufferLookup.IsEmpty())
		{
			return false;
		}
		const auto found = LowerBound(BufferLookup.begin(), BufferLookup.end(), buffer,
			[&](const BufferLookupEntry& indexed, BufferHandle value) noexcept
			{ return Buffers[indexed.Source].Buffer < value; });

		return found != BufferLookup.end() && Buffers[found->Source].Buffer == buffer && found->bConflictingStates;
	}

	bool PreparedRecordingPhase::ContainsBuffer(BufferHandle buffer, EBufferState state, uint64 offset, uint64 bytes) const noexcept
	{
		ASSERT(bBufferLookupReady, "Buffer containment requires validated phase ranges.");
		usize begin = 0;
		usize end = BufferLookup.Size();
		while (begin < end)
		{
			const usize middle = begin + (end - begin) / 2;
			rhi::PreparedBufferAccessDesc const& access = Buffers[BufferLookup[middle].Source];
			if (access.Buffer < buffer || (access.Buffer == buffer && access.Range.ByteOffset <= offset))
			{
				begin = middle + 1;
			}
			else
			{
				end = middle;
			}
		}
		if (begin == 0)
		{
			return false;
		}
		rhi::PreparedAccessProgram::BufferLookupEntry const& indexed = BufferLookup[begin - 1];
		rhi::PreparedBufferAccessDesc const& access = Buffers[indexed.Source];
		// The prefix maximum belongs to one declared range; adjacent ranges never extend each other.
		return access.Buffer == buffer &&
			(access.State == state ||
					(access.State == EBufferState::VertexAndIndex && (state == EBufferState::Vertex || state == EBufferState::Index)) ||
					(access.State == EBufferState::VertexAndReadOnlyStorage && (state == EBufferState::Vertex || state == EBufferState::ReadOnlyStorage))) &&
			offset <= indexed.MaximumEnd &&
			bytes <= indexed.MaximumEnd - offset;
	}

	Span<const uint32> PreparedRecordingPhase::FindTextureAccesses(TextureHandle texture) const noexcept
	{
		if (TextureLookup.IsEmpty())
		{
			return {};
		}
		const auto begin = LowerBound(TextureLookup.begin(), TextureLookup.end(), texture, [&](uint32 index, TextureHandle value) noexcept { return Textures[index].Texture < value; });
		const auto end = UpperBound(begin, TextureLookup.end(), texture, [&](TextureHandle value, uint32 index) noexcept { return value < Textures[index].Texture; });

		return { begin, static_cast<usize>(end - begin) };
	}

	PreparedRecordingList::PreparedRecordingList(MemoryResource& memory) noexcept
		: Phases(memory)
		, NativePhases(memory)
	{
	}

	PreparedRecordingGroup::PreparedRecordingGroup(MemoryResource& memory) noexcept
		: Lists(memory)
		, PlannedAccels(memory)
		, AcquiredImages(memory)
	{
	}

	Result<void> PreparedRecordingGroup::Initialize(const CommandListGroupPreparationDesc& groupDescription, PreparedProgramCache& cache)
	{
		ROX_PROFILE_DEEP_SCOPE("RHI", "InitializePreparedGroup");
		const Span<const CommandListPreparationDesc> descriptions = groupDescription.Lists;
		if (groupDescription.Statistics)
		{
			*groupDescription.Statistics = {};
		}
		Error error;
		error.Code = EErrorCode::ExternalFailure;
		if (groupDescription.QueuePolicy > EPreparedQueuePolicy::MultiQueue || descriptions.IsEmpty() || descriptions.Size() > 65536 || groupDescription.Dependencies.Size() > UINT32_MAX)
		{
			error.Code = EErrorCode::InvalidArgument;

			return Result<void>::Failure(Move(error));
		}
		AcquiredImages.Initialize(static_cast<uint32>(descriptions.Size()));
		QueuePolicy = groupDescription.QueuePolicy;
		Topology = cache.FindTopology(groupDescription);
		const bool bReusedTopology = Topology && Topology->Matches(groupDescription);
		Array<PreparedCommandListDependency> dependencies(Lists.GetResource());
		if (!bReusedTopology)
		{
			if (!MakeSharedInto(Lists.GetResource(), &Topology, Lists.GetResource()) ||
				!Topology->Dependencies.Reserve(groupDescription.Dependencies.Size()) ||
				!Topology->PhaseCounts.Reserve(descriptions.Size()) ||
				!Topology->Offsets.Resize(descriptions.Size()) ||
				!Topology->Counts.Resize(descriptions.Size()))
			{
				return Result<void>::Failure(Move(error));
			}
			Topology->Policy = groupDescription.QueuePolicy;
			for (const rhi::PreparedCommandListDependency& dependency : groupDescription.Dependencies)
			{
				Topology->Dependencies.AddRequired(dependency);
			}
			for (const rhi::CommandListPreparationDesc& description : descriptions)
			{
				Topology->PhaseCounts.AddRequired(static_cast<uint32>(description.Phases.Size()));
			}

			if (!dependencies.Reserve(groupDescription.Dependencies.Size()) || !Topology->Predecessors.Reserve(groupDescription.Dependencies.Size()))
			{
				return Result<void>::Failure(Move(error));
			}
			for (const rhi::PreparedCommandListDependency& dependency : groupDescription.Dependencies)
			{
				if (dependency.Producer >= dependency.Consumer || dependency.Consumer >= descriptions.Size())
				{
					error.Code = EErrorCode::InvalidArgument;

					return Result<void>::Failure(Move(error));
				}
				(void)dependencies.Add(dependency);
			}
			if (!dependencies.IsEmpty())
			{
				Sort(dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) noexcept { return left.Consumer < right.Consumer || (left.Consumer == right.Consumer && left.Producer < right.Producer); });
				for (usize index = 1; index < dependencies.Size(); ++index)
				{
					if (dependencies[index].Consumer == dependencies[index - 1].Consumer && dependencies[index].Producer == dependencies[index - 1].Producer)
					{
						error.Code = EErrorCode::InvalidArgument;

						return Result<void>::Failure(Move(error));
					}
				}
			}
		}

		for (const rhi::CommandListPreparationDesc& description : descriptions)
		{
			if (description.Phases.IsEmpty() || description.Phases.Size() > 65536)
			{
				error.Code = EErrorCode::InvalidArgument;

				return Result<void>::Failure(Move(error));
			}
			for (const rhi::CommandAccessPhaseDesc& phase : description.Phases)
			{
				if (phase.Buffers.Size() > 65536 ||
					phase.Textures.Size() > 65536 ||
					phase.Bindings.Size() > 65536 ||
					phase.Attachments.Size() > 65536 ||
					phase.TextureBufferCopies.Size() > 65536 ||
					phase.Resolves.Size() > 65536 ||
					phase.Indirects.Size() > 65536 ||
					phase.AccelStructs.Size() > 65536 ||
					phase.AccelBuilds.Size() > 65536 ||
					phase.AccelCompactionQueries.Size() > 65536 ||
					phase.AccelCompactions.Size() > 65536)
				{
					error.Code = EErrorCode::Unsupported;

					return Result<void>::Failure(Move(error));
				}
			}
		}

		if (!Lists.Reserve(descriptions.Size()))
		{
			return Result<void>::Failure(Move(error));
		}
		for (const rhi::CommandListPreparationDesc& description : descriptions)
		{
			PreparedRecordingList list(Lists.GetResource());
			list.Handle = description.List;
			if (!list.Phases.Reserve(description.Phases.Size()))
			{
				return Result<void>::Failure(Move(error));
			}
			for (const rhi::CommandAccessPhaseDesc& source : description.Phases)
			{
				SharedPtr<PreparedAccessProgram> program = cache.FindAccess(source);
				if (!program)
				{
					if (!MakeSharedInto(Lists.GetResource(), &program, Lists.GetResource()))
					{
						return Result<void>::Failure(Move(error));
					}
					// Retain up to four set generations per declared binding, capped at 4,096 entries.
					program->FixedSetCoverageLimit = Min<usize>(source.Bindings.Size(), 128) * MAXIMUM_FIXED_BINDING_LAYOUTS * 4;
				}
				if (groupDescription.Statistics)
				{
					if (program->bValidated)
					{
						++groupDescription.Statistics->AccessProgramReuses;
					}
					else
					{
						++groupDescription.Statistics->AccessProgramBuilds;
					}
				}
				PreparedRecordingPhase phase(Lists.GetResource(), Move(program));
				phase.Capacity = source.Capacity;
				phase.GpuTimer = source.GpuTimer;
				if (!phase.Buffers.Reserve(source.Buffers.Size()) ||
					!phase.Textures.Reserve(source.Textures.Size()) ||
					!phase.Attachments.Reserve(source.Attachments.Size()) ||
					!phase.Bindings.Reserve(source.Bindings.Size()) ||
					!phase.TextureBufferCopies.Reserve(source.TextureBufferCopies.Size()) ||
					!phase.Resolves.Reserve(source.Resolves.Size()) ||
					!phase.Indirects.Reserve(source.Indirects.Size()) ||
					!phase.AccelStructs.Reserve(source.AccelStructs.Size()) ||
					!phase.AccelBuilds.Reserve(source.AccelBuilds.Size()) ||
					!phase.AccelCompactionQueries.Reserve(source.AccelCompactionQueries.Size()) ||
					!phase.AccelCompactions.Reserve(source.AccelCompactions.Size()))
				{
					return Result<void>::Failure(Move(error));
				}
				if (!phase.AccessProgram->bValidated)
				{
					for (const rhi::PreparedBufferAccessDesc& value : source.Buffers)
					{
						(void)phase.Buffers.Add(value);
					}
					for (const rhi::PreparedTextureAccessDesc& value : source.Textures)
					{
						(void)phase.Textures.Add(value);
					}
				}
				for (const rhi::PreparedTextureBufferCopyDesc& value : source.TextureBufferCopies)
				{
					(void)phase.TextureBufferCopies.Add({ value, {} });
				}
				for (const rhi::TextureResolve& value : source.Resolves)
				{
					(void)phase.Resolves.Add({ value, {} });
				}
				for (const rhi::PreparedIndirectDesc& value : source.Indirects)
				{
					(void)phase.Indirects.Add({ value, {} });
				}
				for (const rhi::PreparedAccelStructAccessDesc& value : source.AccelStructs)
				{
					(void)phase.AccelStructs.Add(value);
				}
				for (const rhi::PreparedAccelStructCompactionQueryDesc& value : source.AccelCompactionQueries)
				{
					(void)phase.AccelCompactionQueries.Add({ value });
				}
				for (const rhi::PreparedAccelStructCompactDesc& value : source.AccelCompactions)
				{
					(void)phase.AccelCompactions.Add({ value });
				}
				for (const rhi::PreparedAccelStructBuildDesc& value : source.AccelBuilds)
				{
					usize geometryCount = 0;
					if (!AddMemorySize(value.Build.Triangles.Size(), value.Build.Aabbs.Size(), &geometryCount) || geometryCount > 65536 || value.ReferencedBlas.Size() > 65536)
					{
						error.Code = EErrorCode::Unsupported;

						return Result<void>::Failure(Move(error));
					}

					PreparedRecordingAccelBuild build(Lists.GetResource());
					build.Description = value;
					build.Description.Build.Triangles = {};
					build.Description.Build.Aabbs = {};
					build.Description.ReferencedBlas = {};
					if (!build.Triangles.Reserve(value.Build.Triangles.Size()) || !build.Aabbs.Reserve(value.Build.Aabbs.Size()) || !build.ReferencedBlas.Reserve(value.ReferencedBlas.Size()))
					{
						return Result<void>::Failure(Move(error));
					}

					for (const rhi::AccelTriangleGeometryDesc& triangle : value.Build.Triangles)
					{
						if (triangle.OpacityMicromap.IsValid() || triangle.OpacityMicromapIndexBuffer.IsValid() || !triangle.OpacityMicromapUsageCounts.IsEmpty())
						{
							error.Code = EErrorCode::Unsupported;

							return Result<void>::Failure(Move(error));
						}
						(void)build.Triangles.Add(triangle);
					}
					for (const rhi::AccelAabbGeometryDesc& aabb : value.Build.Aabbs)
					{
						(void)build.Aabbs.Add(aabb);
					}
					for (const rhi::AccelStructHandle handle : value.ReferencedBlas)
					{
						(void)build.ReferencedBlas.Add(handle);
					}
					(void)phase.AccelBuilds.Add(Move(build));
				}
				for (const rhi::RenderPassLayout& value : source.Attachments)
				{
					ASSERT(value.DepthLoad <= EAttachmentLoad::Clear && value.StencilLoad <= EAttachmentLoad::Clear, "Prepared depth and stencil load operations are valid.");
					ASSERT(value.ColorLoads.Size() <= 8, "Prepared color load count is within the framebuffer limit.");

					PreparedRecordingAttachment attachment;
					attachment.Framebuffer = value.Framebuffer;
					attachment.ColorCount = static_cast<uint32>(value.ColorLoads.Size());
					attachment.DepthLoad = value.DepthLoad == EAttachmentLoad::Clear ? EDepthLoadOp::Clear : EDepthLoadOp::Load;
					attachment.bReadOnlyDepth = value.bReadOnlyDepth;
					attachment.StencilLoad = value.StencilLoad == EAttachmentLoad::Clear ? EDepthLoadOp::Clear : EDepthLoadOp::Load;
					attachment.bReadOnlyStencil = value.bReadOnlyStencil;
					for (uint32 color = 0; color < attachment.ColorCount; ++color)
					{
						ASSERT(value.ColorLoads[color] <= EAttachmentLoad::Clear, "Prepared color load operations are valid.");
						attachment.ColorLoads[color] = value.ColorLoads[color];
					}

					(void)phase.Attachments.Add(attachment);
				}
				for (const rhi::PreparedBindingDesc& input : source.Bindings)
				{
					PreparedBinding binding;
					if (const ComputePipelineHandle* const compute = GetIf<ComputePipelineHandle>(&input.Pipeline))
					{
						binding.ComputePipeline = *compute;
					}
					else if (const GraphicsPipelineHandle* const graphics = GetIf<GraphicsPipelineHandle>(&input.Pipeline))
					{
						binding.GraphicsPipeline = *graphics;
					}
					else if (const ShaderTableHandle* const table = GetIf<ShaderTableHandle>(&input.Pipeline))
					{
						binding.ShaderTable = *table;
					}
					binding.SetCount = static_cast<uint32>(input.BindingSets.Size());
					if (!input.DeviceHeaps.IsEmpty())
					{
						if (input.DeviceHeaps.Size() > 32)
						{
							error.Code = EErrorCode::InvalidArgument;

							return Result<void>::Failure(Move(error));
						}
						uint64 usedCount = 0;
						for (const rhi::DescriptorHeapBinding& heap : input.DeviceHeaps)
						{
							usedCount += heap.UsedEntries.Size();
						}
						if (usedCount > UINT32_MAX)
						{
							error.Code = EErrorCode::InvalidArgument;

							return Result<void>::Failure(Move(error));
						}
						if (!MakeSharedInto(Lists.GetResource(), &binding.DeviceHeaps, Lists.GetResource()) ||
							!binding.DeviceHeaps->Ranges.Reserve(input.DeviceHeaps.Size()) ||
							!binding.DeviceHeaps->UsedEntries.Reserve(static_cast<usize>(usedCount)))
						{
							return Result<void>::Failure(Move(error));
						}

						for (const rhi::DescriptorHeapBinding& heap : input.DeviceHeaps)
						{
							BoundDescriptorTableRange row;
							row.Table = heap.Table;
							row.UsedOffset = static_cast<uint32>(binding.DeviceHeaps->UsedEntries.Size());
							row.UsedCount = static_cast<uint32>(heap.UsedEntries.Size());
							for (const uint32 entry : heap.UsedEntries)
							{
								binding.DeviceHeaps->UsedEntries.AddRequired(entry);
							}
							binding.DeviceHeaps->Ranges.AddRequired(row);
						}
					}
					if (input.BindingSets.Size() > MAXIMUM_FIXED_BINDING_LAYOUTS || input.VertexBuffers.Size() > 4)
					{
						error.Code = EErrorCode::Unsupported;

						return Result<void>::Failure(Move(error));
					}
					for (uint32 index = 0; index < binding.SetCount; ++index)
					{
						binding.Sets[index] = input.BindingSets[index];
					}
					binding.VertexBufferCount = static_cast<uint32>(input.VertexBuffers.Size());
					for (uint32 index = 0; index < binding.VertexBufferCount; ++index)
					{
						binding.VertexBuffers[index] = input.VertexBuffers[index];
					}
					binding.IndexBuffer = input.IndexBuffer;
					binding.IndexRange = input.IndexRange;
					binding.IndexFormat = input.IndexFormat;
					(void)phase.Bindings.Add(binding);
				}
				(void)list.Phases.Add(Move(phase));
			}
			(void)Lists.Add(Move(list));
		}
		if (!bReusedTopology)
		{
			for (rhi::PreparedCommandListDependency const& dependency : dependencies)
			{
				++Topology->Counts[dependency.Consumer];
				Topology->Predecessors.AddRequired(dependency.Producer);
			}
			uint32 offset = 0;
			for (usize index = 0; index < Lists.Size(); ++index)
			{
				Topology->Offsets[index] = offset;
				offset += Topology->Counts[index];
			}
		}
		Predecessors = Topology->Predecessors.View();
		for (usize index = 0; index < Lists.Size(); ++index)
		{
			Lists[index].PredecessorOffset = Topology->Offsets[index];
			Lists[index].PredecessorCount = Topology->Counts[index];
		}

		return Result<void>::Success();
	}

	void PreparedRecordingGroup::BuildDependencyFrontier(uint32 listIndex) noexcept
	{
		ASSERT(listIndex < Lists.Size(), "Dependency preparation selects an existing list.");
		rhi::PreparedRecordingList& list = Lists[listIndex];
		const uint32 lane = static_cast<uint32>(list.Queue);
		ASSERT(lane < 3, "Every prepared list uses a supported queue.");
		const auto inherit = [&](uint32 predecessor) noexcept
			{
				ASSERT(predecessor < listIndex, "Prepared list dependencies are topologically ordered.");
				for (uint32 queueIndex = 0; queueIndex < 3; ++queueIndex)
				{
					list.DependencyFrontier[queueIndex] = Max(list.DependencyFrontier[queueIndex], Lists[predecessor].DependencyFrontier[queueIndex]);
				}
			};
		if (m_LastQueueList[lane] != 0xffffffffu)
		{
			inherit(m_LastQueueList[lane]);
		}
		for (uint32 predecessor = 0; predecessor < list.PredecessorCount; ++predecessor)
		{
			inherit(Predecessors[list.PredecessorOffset + predecessor]);
		}
		list.DependencyFrontier[lane] = listIndex + 1;
		m_LastQueueList[lane] = listIndex;
	}

	bool PreparedRecordingGroup::HappensBefore(uint32 producer, uint32 consumer) const noexcept
	{
		ASSERT(producer < Lists.Size() && consumer < Lists.Size(), "Dependency queries select existing lists.");

		return producer < consumer && Lists[consumer].DependencyFrontier[static_cast<uint32>(Lists[producer].Queue)] >= producer + 1;
	}
}
