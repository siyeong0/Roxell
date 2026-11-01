#include "PreparedProgramCache.h"

#if ROXELL_RHI_BACKEND == 1
#include "D3D12/D3D12Submission.h"
#elif ROXELL_RHI_BACKEND == 2
#include "Vulkan/VulkanSubmission.h"
#endif
#include <UniquePtr.hpp>

#include "PreparedRecordingGroup.h"

#include <Assertion.h>
#include <Hashing.hpp>

namespace rox::rhi
{
	using namespace core;

	bool PreparedBindingProgram::Matches(const PreparedBinding& binding) const noexcept
	{
		if (binding.DeviceHeaps ||
			binding.ShaderTable.IsValid() ||
			Shape.ComputePipeline != binding.ComputePipeline ||
			Shape.GraphicsPipeline != binding.GraphicsPipeline ||
			Shape.SetCount != binding.SetCount ||
			Shape.VertexBufferCount != binding.VertexBufferCount ||
			Shape.IndexBuffer != binding.IndexBuffer ||
			Shape.IndexRange.ByteOffset != binding.IndexRange.ByteOffset ||
			Shape.IndexRange.ByteCount != binding.IndexRange.ByteCount ||
			Shape.IndexFormat != binding.IndexFormat)
		{
			return false;
		}
		for (uint32 i = 0; i < binding.SetCount; ++i)
		{
			if (Shape.Sets[i] != binding.Sets[i])
			{
				return false;
			}
		}
		for (uint32 i = 0; i < binding.VertexBufferCount; ++i)
		{
			if (Shape.VertexBuffers[i].Buffer != binding.VertexBuffers[i].Buffer ||
				Shape.VertexBuffers[i].Range.ByteOffset != binding.VertexBuffers[i].Range.ByteOffset ||
				Shape.VertexBuffers[i].Range.ByteCount != binding.VertexBuffers[i].Range.ByteCount)
			{
				return false;
			}
		}
		return true;
	}

	bool PreparedAccessProgram::Matches(const CommandAccessPhaseDesc& desc) const noexcept
	{
		if (!bValidated || Buffers.Size() != desc.Buffers.Size() || Textures.Size() != desc.Textures.Size())
		{
			return false;
		}
		for (usize index = 0; index < Buffers.Size(); ++index)
		{
			const PreparedBufferAccessDesc& a = Buffers[index];
			const PreparedBufferAccessDesc& b = desc.Buffers[index];
			if (a.Buffer != b.Buffer || a.State != b.State || a.Range.ByteOffset != b.Range.ByteOffset || a.Range.ByteCount != b.Range.ByteCount)
			{
				return false;
			}
		}
		for (usize index = 0; index < Textures.Size(); ++index)
		{
			const PreparedTextureAccessDesc& a = Textures[index];
			const PreparedTextureAccessDesc& b = desc.Textures[index];
			if (a.Texture != b.Texture ||
				a.State != b.State ||
				a.Aspect != b.Aspect ||
				a.BaseMipLevel != b.BaseMipLevel ||
				a.MipLevelCount != b.MipLevelCount ||
				a.BaseArrayLayer != b.BaseArrayLayer ||
				a.ArrayLayerCount != b.ArrayLayerCount)
			{
				return false;
			}
		}
		return true;
	}

	bool PreparedGroupTopology::Matches(const CommandListGroupPreparationDesc& desc) const noexcept
	{
		if (Policy != desc.QueuePolicy || PhaseCounts.Size() != desc.Lists.Size() || Dependencies.Size() != desc.Dependencies.Size())
		{
			return false;
		}
		for (usize index = 0; index < PhaseCounts.Size(); ++index)
		{
			if (PhaseCounts[index] != desc.Lists[index].Phases.Size())
			{
				return false;
			}
		}
		for (usize index = 0; index < Dependencies.Size(); ++index)
		{
			if (Dependencies[index].Producer != desc.Dependencies[index].Producer || Dependencies[index].Consumer != desc.Dependencies[index].Consumer)
			{
				return false;
			}
		}
		return true;
	}

	namespace
	{
		void hashValue(uint64& hash, uint64 value) noexcept
		{
			hash = (hash ^ value) * 1099511628211ull;
		}

		template <typename THandle> void hashHandle(uint64& hash, THandle handle) noexcept
		{
			hashValue(hash, handle.Owner);
			hashValue(hash, (uint64(handle.Generation) << 32) | handle.Slot);
		}

		uint64 bindingHash(const PreparedBinding& binding) noexcept
		{
			uint64 hash = 14695981039346656037ull;
			hashHandle(hash, binding.ComputePipeline);
			hashHandle(hash, binding.GraphicsPipeline);
			hashValue(hash, binding.SetCount);
			for (uint32 index = 0; index < binding.SetCount; ++index)
			{
				hashHandle(hash, binding.Sets[index]);
			}
			hashValue(hash, binding.VertexBufferCount);
			for (uint32 index = 0; index < binding.VertexBufferCount; ++index)
			{
				const PreparedVertexBufferBinding& vertex = binding.VertexBuffers[index];
				hashHandle(hash, vertex.Buffer);
				hashValue(hash, vertex.Range.ByteOffset);
				hashValue(hash, vertex.Range.ByteCount);
			}
			hashHandle(hash, binding.IndexBuffer);
			hashValue(hash, binding.IndexRange.ByteOffset);
			hashValue(hash, binding.IndexRange.ByteCount);
			hashValue(hash, static_cast<uint64>(binding.IndexFormat));
			return HashCombine(0, hash);
		}

		uint64 accessHash(Span<const PreparedBufferAccessDesc> buffers, Span<const PreparedTextureAccessDesc> textures) noexcept
		{
			uint64 hash = 14695981039346656037ull;
			hashValue(hash, buffers.Size());
			hashValue(hash, textures.Size());
			for (const PreparedBufferAccessDesc& buffer : buffers)
			{
				hashHandle(hash, buffer.Buffer);
				hashValue(hash, static_cast<uint64>(buffer.State));
				hashValue(hash, buffer.Range.ByteOffset);
				hashValue(hash, buffer.Range.ByteCount);
			}
			for (const PreparedTextureAccessDesc& texture : textures)
			{
				hashHandle(hash, texture.Texture);
				hashValue(hash, static_cast<uint64>(texture.State));
				hashValue(hash, static_cast<uint64>(texture.Aspect));
				hashValue(hash, texture.BaseMipLevel);
				hashValue(hash, texture.MipLevelCount);
				hashValue(hash, texture.BaseArrayLayer);
				hashValue(hash, texture.ArrayLayerCount);
			}
			return HashCombine(0, hash);
		}

		uint64 topologyHash(const CommandListGroupPreparationDesc& group) noexcept
		{
			uint64 hash = static_cast<uint64>(group.QueuePolicy);
			for (const CommandListPreparationDesc& list : group.Lists)
			{
				hashValue(hash, list.Phases.Size());
			}
			for (const PreparedCommandListDependency& dependency : group.Dependencies)
			{
				hashValue(hash, dependency.Producer);
				hashValue(hash, dependency.Consumer);
			}
			return HashCombine(0, hash);
		}

		template <typename TProgram> void retainProgram(
			SharedPtr<TProgram>(&bucket)[4],
			uint8& next,
			usize& bytes,
			usize maximumBytes,
			const SharedPtr<TProgram>& program) noexcept
		{
			for (const SharedPtr<TProgram>& entry : bucket)
			{
				if (entry.Get() == program.Get())
				{
					return;
				}
			}
			SharedPtr<TProgram>& replaced = bucket[next];
			const usize previousBytes = replaced ? replaced->CacheBytes : 0;
			ASSERT(bytes >= previousBytes && bytes <= maximumBytes, "Cache accounting stays within its budget.");
			if (program->CacheBytes > maximumBytes - (bytes - previousBytes))
			{
				return;
			}
			bytes = bytes - previousBytes + program->CacheBytes;
			replaced = program;
			next = (next + 1) % 4;
		}
	}

	PreparedProgramCache::PreparedProgramCache(MemoryResource& memory) noexcept
		: m_Bindings(memory)
		, m_Accesses(memory)
		, m_Topologies(memory)
	{
	}

	SharedPtr<PreparedBindingProgram> PreparedProgramCache::FindBinding(const PreparedBinding& binding) const noexcept
	{
		if (binding.DeviceHeaps || binding.ShaderTable.IsValid())
		{
			return {};
		}
		const uint64 hash = bindingHash(binding);
		if (m_Bindings.IsEmpty())
		{
			return {};
		}
		for (const SharedPtr<PreparedBindingProgram>& program : m_Bindings[hash % BINDING_BUCKETS].Programs)
		{
			if (program && program->CacheHash == hash && program->Matches(binding))
			{
				return program;
			}
		}
		return {};
	}

	SharedPtr<PreparedAccessProgram> PreparedProgramCache::FindAccess(const CommandAccessPhaseDesc& phase) const noexcept
	{
		if (!phase.AccelStructs.IsEmpty() || !phase.AccelBuilds.IsEmpty() || !phase.AccelCompactionQueries.IsEmpty() || !phase.AccelCompactions.IsEmpty())
		{
			return {};
		}
		const uint64 hash = accessHash(phase.Buffers, phase.Textures);
		if (m_Accesses.IsEmpty())
		{
			return {};
		}
		for (const SharedPtr<PreparedAccessProgram>& program : m_Accesses[hash % ACCESS_BUCKETS].Programs)
		{
			if (program && program->CacheHash == hash && program->Matches(phase))
			{
				return program;
			}
		}
		return {};
	}

	SharedPtr<PreparedGroupTopology> PreparedProgramCache::FindTopology(const CommandListGroupPreparationDesc& group) const noexcept
	{
		const uint64 hash = topologyHash(group);
		if (m_Topologies.IsEmpty())
		{
			return {};
		}
		for (const SharedPtr<PreparedGroupTopology>& program : m_Topologies[hash % TOPOLOGY_BUCKETS].Programs)
		{
			if (program && program->CacheHash == hash && program->Matches(group))
			{
				return program;
			}
		}
		return {};
	}

	void PreparedProgramCache::Retain(const PreparedRecordingGroup& group) noexcept
	{
		for (const PreparedRecordingList& list : group.Lists)
		{
			for (const PreparedRecordingPhase& phase : list.Phases)
			{
				const SharedPtr<PreparedAccessProgram>& access = phase.AccessProgram;
				if (phase.AccelStructs.IsEmpty() && phase.AccelBuilds.IsEmpty() && phase.AccelCompactionQueries.IsEmpty() && phase.AccelCompactions.IsEmpty() && access->CacheBytes == 0)
				{
					access->CacheHash = accessHash(access->Buffers.View(), access->Textures.View());
					// Charge the memo's bounded hash slots, occupancy slack and control bytes before it fills.
					access->CacheBytes = sizeof(PreparedAccessProgram) + access->FixedSetCoverageLimit * 96 +
						access->Buffers.Capacity() * sizeof(PreparedBufferAccessDesc) +
						access->Textures.Capacity() * sizeof(PreparedTextureAccessDesc) +
						access->BufferLookup.Capacity() * sizeof(PreparedAccessProgram::BufferLookupEntry) +
						access->TextureLookup.Capacity() * sizeof(uint32) +
						access->NativeBuffers.Capacity() * sizeof(NativePreparedBufferAccess) +
						access->NativeTextures.Capacity() * sizeof(NativePreparedTextureAccess);
				}
				if (access->CacheBytes != 0 && (!m_Accesses.IsEmpty() || m_Accesses.Resize(ACCESS_BUCKETS)))
				{
					const uint64 bucket = access->CacheHash % ACCESS_BUCKETS;
					retainProgram(m_Accesses[bucket].Programs, m_Accesses[bucket].Next, m_AccessBytes, 32 * 1024 * 1024, access);
				}
				for (const PreparedBinding& binding : phase.Bindings)
				{
					const SharedPtr<PreparedBindingProgram>& program = binding.Execution;
					if (!program || !program->Program || (m_Bindings.IsEmpty() && !m_Bindings.Resize(BINDING_BUCKETS)))
					{
						continue;
					}
					if (program->CacheBytes == 0)
					{
						program->CacheHash = bindingHash(program->Shape);
						program->CacheBytes = sizeof(PreparedBindingProgram) + sizeof(NativeBindingProgram) +
							program->Program->RetainedBytes +
							program->ResourceDescriptorCount * 64 + program->SamplerDescriptorCount * 64;
					}
					const uint64 bucket = program->CacheHash % BINDING_BUCKETS;
					retainProgram(m_Bindings[bucket].Programs, m_Bindings[bucket].Next, m_BindingBytes, 128 * 1024 * 1024, program);
				}
			}
		}
		if (m_Topologies.IsEmpty() && !m_Topologies.Resize(TOPOLOGY_BUCKETS))
		{
			return;
		}
		const SharedPtr<PreparedGroupTopology>& topology = group.Topology;
		if (topology->CacheBytes == 0)
		{
			uint64 hash = static_cast<uint64>(topology->Policy);
			for (const uint32 count : topology->PhaseCounts)
			{
				hashValue(hash, count);
			}
			for (const PreparedCommandListDependency& dependency : topology->Dependencies)
			{
				hashValue(hash, dependency.Producer);
				hashValue(hash, dependency.Consumer);
			}
			topology->CacheHash = HashCombine(0, hash);
			topology->CacheBytes = sizeof(PreparedGroupTopology) +
				topology->Dependencies.Capacity() * sizeof(PreparedCommandListDependency) +
				(topology->PhaseCounts.Capacity() + topology->Predecessors.Capacity() +
					topology->Offsets.Capacity() + topology->Counts.Capacity()) * sizeof(uint32);
		}
		const uint64 bucket = topology->CacheHash % TOPOLOGY_BUCKETS;
		retainProgram(m_Topologies[bucket].Programs, m_Topologies[bucket].Next, m_TopologyBytes, 1024 * 1024, topology);
	}

	void PreparedProgramCache::Clear() noexcept
	{
		for (PreparedProgramCache::Bucket<PreparedBindingProgram>& bucket : m_Bindings)
		{
			for (SharedPtr<PreparedBindingProgram>& program : bucket.Programs)
			{
				program.Reset();
			}
		}
		for (PreparedProgramCache::Bucket<PreparedAccessProgram>& bucket : m_Accesses)
		{
			for (SharedPtr<PreparedAccessProgram>& program : bucket.Programs)
			{
				program.Reset();
			}
		}
		for (PreparedProgramCache::Bucket<PreparedGroupTopology>& bucket : m_Topologies)
		{
			for (SharedPtr<PreparedGroupTopology>& program : bucket.Programs)
			{
				program.Reset();
			}
		}
		m_BindingBytes = 0;
		m_AccessBytes = 0;
		m_TopologyBytes = 0;
	}
}

namespace rox::rhi
{
	using namespace core;

	NativeBindingProgram::~NativeBindingProgram() noexcept
	{
		if (Data == nullptr)
		{
			return;
		}
		ASSERT(Resource != nullptr, "A prepared binding payload retains its allocator.");
#if ROXELL_RHI_BACKEND == 1
		DestroyObject(*Resource, static_cast<d3d12::Pending::PreparedBinding*>(Data));
#elif ROXELL_RHI_BACKEND == 2
		DestroyObject(*Resource, static_cast<vulkan::Pending::PreparedBinding*>(Data));
#else
		ASSERT(false, "The Null backend does not create prepared binding payloads.");
#endif
	}
}
