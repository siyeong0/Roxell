#pragma once

#include <Array.hpp>
#include <RhiTypes.h>
#include <SharedPtr.hpp>
#include <Types.h>

#if ROXELL_RHI_BACKEND == 1
namespace rox::d3d12
{
	struct State;
}
#elif ROXELL_RHI_BACKEND == 2
namespace rox::vulkan
{
	struct State;
}
#else
namespace rox::nullrhi
{
	struct State;
}
#endif

namespace rox::rhi
{
#if ROXELL_RHI_BACKEND == 1
	using SelectedBackendState = d3d12::State;
#elif ROXELL_RHI_BACKEND == 2
	using SelectedBackendState = vulkan::State;
#else
	using SelectedBackendState = nullrhi::State;
#endif

	enum class EPreparedDescriptorStorage
	{
		FixedSet,
		TableSnapshot
	};

	class PreparedDescriptorSet final
	{
	public:
		PreparedDescriptorSet(SelectedBackendState* backend, void* native, EPreparedDescriptorStorage storage = EPreparedDescriptorStorage::FixedSet) noexcept;
		~PreparedDescriptorSet() noexcept;
		PreparedDescriptorSet(const PreparedDescriptorSet&) = delete;
		PreparedDescriptorSet& operator=(const PreparedDescriptorSet&) = delete;

		[[nodiscard]] void* GetNative() const noexcept { return m_Native; }

	private:
		SelectedBackendState* m_Backend;
		void* m_Native;
		EPreparedDescriptorStorage m_Storage;
	};

	struct NativeFixedDescriptor final
	{
		void* Resource = nullptr;
		void* View = nullptr;

		core::uint64 ByteOffset = 0;
		core::uint64 ByteCount = 0;

		bool bUsed = true;
	};

	class PreparedBindingCache final
	{
	public:
		PreparedBindingCache() noexcept = default;

		[[nodiscard]] const core::SharedPtr<PreparedDescriptorSet>& Get(core::uint32 index) const noexcept;
		void Capture(const core::SharedPtr<PreparedDescriptorSet>& owner) noexcept;

	public:
		static constexpr core::uint32 CAPACITY = 8;

		// The owning binding set pins these native objects and cannot mutate its fixed descriptors.
		// Tables and acceleration structures use their execution-dependent path instead.
		NativeFixedDescriptor Descriptors[32]{};
		core::uint32 DescriptorCount = 0;
		bool bDescriptorsReady = false;

	private:
		core::SharedPtr<PreparedDescriptorSet> m_Entries[CAPACITY];
	};

	struct BoundDescriptorTableRange final
	{
		core::uint32 RangeIndex = 0;
		DescriptorTableHandle Table;

		core::uint32 UsedOffset = 0;
		core::uint32 UsedCount = 0;

		core::uint32 MaskOffset = 0;
	};

	struct BoundDescriptorTables final
	{
		explicit BoundDescriptorTables(core::MemoryResource& memory) noexcept
			: Ranges(memory)
			, UsedEntries(memory)
			, UsedMask(memory)
		{
		}

		BoundDescriptorTables(const BoundDescriptorTables&) = delete;
		BoundDescriptorTables& operator=(const BoundDescriptorTables&) = delete;

		core::Array<BoundDescriptorTableRange> Ranges;
		core::Array<core::uint32> UsedEntries;
		core::Array<core::Byte> UsedMask;
	};
}
