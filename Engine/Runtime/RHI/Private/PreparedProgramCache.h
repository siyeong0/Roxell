#pragma once

#include <RhiCommandPreparation.h>
#include <Array.hpp>
#include <HashMap.hpp>
#include <MemoryResource.h>
#include <SharedPtr.hpp>
#include "DeviceCommands.h"
#include "PreparedBindingCache.h"

namespace rox::rhi
{
	struct PreparedBindingProgram;
	class PreparedRecordingGroup;

	struct PreparedBinding final
	{
		core::SharedPtr<PreparedBindingProgram> Execution;

		ComputePipelineHandle ComputePipeline;
		GraphicsPipelineHandle GraphicsPipeline;
		ShaderTableHandle ShaderTable;

		BindingSetHandle Sets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 SetCount = 0;

		core::SharedPtr<BoundDescriptorTables> DeviceHeaps;

		void* NativePipeline = nullptr;
		core::uint32 NativeIndex = 0;

		core::uint32 PushConstantBytes = 0;

		bool bMeshShader = false;
		bool bTaskShader = false;

		PreparedVertexBufferBinding VertexBuffers[4]{};
		core::uint32 VertexBufferCount = 0;
		core::uint64 VertexElementCounts[4]{};
		bool bPerInstance[4]{};

		BufferHandle IndexBuffer;
		BufferRange IndexRange;
		EFormat IndexFormat = EFormat::R16_UINT;

		NativePreparedVertexInput NativeVertexInput;
	};

	struct NativeBindingProgram final
	{
		void* Data = nullptr;
		core::MemoryResource* Resource = nullptr;
		core::usize RetainedBytes = 0;

		~NativeBindingProgram() noexcept;
		NativeBindingProgram() noexcept = default;
		NativeBindingProgram(const NativeBindingProgram&) = delete;
		NativeBindingProgram& operator=(const NativeBindingProgram&) = delete;
	};

	struct PreparedBindingProgram final
	{
		core::uint64 CacheHash = 0;
		core::usize CacheBytes = 0;

		PreparedBinding Shape;
		core::SharedPtr<NativeBindingProgram> Program;

		core::uint64 ResourceDescriptorCount = 0;
		core::uint64 SamplerDescriptorCount = 0;
		bool bDescriptorCountsReady = false;
		[[nodiscard]] bool Matches(const PreparedBinding& binding) const noexcept;
	};

	struct PreparedAccessProgram final
	{
		core::uint64 CacheHash = 0;
		core::usize CacheBytes = 0;

		struct BufferLookupEntry final
		{
			core::uint32 Source = 0;
			core::uint64 MaximumEnd = 0;
			bool bConflictingStates = false;
		};

		explicit PreparedAccessProgram(core::MemoryResource& memory) noexcept
			: Buffers(memory)
			, Textures(memory)
			, BufferLookup(memory)
			, TextureLookup(memory)
			, NativeBuffers(memory)
			, NativeTextures(memory)
			, FixedSetCoverage(memory)
		{
		}

		core::Array<PreparedBufferAccessDesc> Buffers;
		core::Array<PreparedTextureAccessDesc> Textures;

		core::Array<BufferLookupEntry> BufferLookup;
		core::Array<core::uint32> TextureLookup;

		core::Array<NativePreparedBufferAccess> NativeBuffers;
		core::Array<NativePreparedTextureAccess> NativeTextures;

		// Control-thread memo of structural coverage, keyed by live immutable binding-set generation.
		core::HashMap<core::uint64, BindingSetHandle> FixedSetCoverage;
		core::usize FixedSetCoverageLimit = 0;

		EQueue ValidatedQueue = EQueue::Graphics;
		bool bValidated = false;

		[[nodiscard]] bool Matches(const CommandAccessPhaseDesc& desc) const noexcept;
	};

	struct PreparedGroupTopology final
	{
		core::uint64 CacheHash = 0;
		core::usize CacheBytes = 0;

		explicit PreparedGroupTopology(core::MemoryResource& memory) noexcept
			: Dependencies(memory)
			, PhaseCounts(memory)
			, Predecessors(memory)
			, Offsets(memory)
			, Counts(memory)
		{
		}

		core::Array<PreparedCommandListDependency> Dependencies;
		core::Array<core::uint32> PhaseCounts;

		core::Array<core::uint32> Predecessors;
		core::Array<core::uint32> Offsets;
		core::Array<core::uint32> Counts;

		EPreparedQueuePolicy Policy = EPreparedQueuePolicy::SingleQueue;

		[[nodiscard]] bool Matches(const CommandListGroupPreparationDesc& desc) const noexcept;
	};

	// Device-owned, bounded caches. Executions retain selected programs independently.
	class PreparedProgramCache final
	{
	public:
		explicit PreparedProgramCache(core::MemoryResource& memory) noexcept;
		[[nodiscard]] core::SharedPtr<PreparedBindingProgram> FindBinding(const PreparedBinding& binding) const noexcept;
		[[nodiscard]] core::SharedPtr<PreparedAccessProgram> FindAccess(const CommandAccessPhaseDesc& phase) const noexcept;
		[[nodiscard]] core::SharedPtr<PreparedGroupTopology> FindTopology(const CommandListGroupPreparationDesc& group) const noexcept;
		void Retain(const PreparedRecordingGroup& group) noexcept;
		void Clear() noexcept;

	private:
		static constexpr core::usize BINDING_BUCKETS = 8192;
		static constexpr core::usize ACCESS_BUCKETS = 1024;
		static constexpr core::usize TOPOLOGY_BUCKETS = 16;

		template <typename TProgram> struct Bucket final
		{
			core::SharedPtr<TProgram> Programs[4];
			core::uint8 Next = 0;
		};

		core::Array<Bucket<PreparedBindingProgram>> m_Bindings;
		core::Array<Bucket<PreparedAccessProgram>> m_Accesses;
		core::Array<Bucket<PreparedGroupTopology>> m_Topologies;

		core::usize m_BindingBytes = 0;
		core::usize m_AccessBytes = 0;
		core::usize m_TopologyBytes = 0;
	};
}
