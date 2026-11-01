#include "DeviceResources.h"

#include <Assertion.h>
#include <Hash.h>

namespace rox::rhi
{
	using namespace core;

	BufferSlot::BufferSlot(MemoryResource& resource) noexcept
		: ClusterMicromapReferences(resource)
	{
	}

	AccelStructSlot::AccelStructSlot(MemoryResource& resource) noexcept
		: ClusterReferences(resource)
		, ClusterBottomReferences(resource)
		, BottomReferences(resource)
		, OpacityMicromapReferences(resource)
		, PreparedBuildShape(resource)
		, PreparedBottomReferences(resource)
	{
	}

	PreparedTextureMappings::PreparedTextureMappings(MemoryResource& memory) noexcept
		: Mappings(memory)
		, NativeMappings(memory)
		, RetainedHeaps(memory)
	{
	}

	PreparedTextureMappings::~PreparedTextureMappings()
	{
		ASSERT(NativeStorage.IsEmpty() && !bPinsAcquired, "Prepared mapping native storage and pins are released by the device control thread.");
	}

	[[nodiscard]] usize SparseTileHash::operator()(const SparseTileKey& key) const noexcept
	{
		uint64 hash = HashCombine(0, key.Texture.Owner);
		hash = HashCombine(hash, key.Texture.Slot);
		hash = HashCombine(hash, key.Texture.Generation);
		hash = HashCombine(hash, key.MipLevel);
		hash = HashCombine(hash, key.ArrayLayer);
		hash = HashCombine(hash, key.X);
		hash = HashCombine(hash, key.Y);

		return static_cast<usize>(HashCombine(hash, key.Z));
	}

	ShaderSlot::ShaderSlot(MemoryResource& resource) noexcept
		: Bytecode(resource)
	{
	}

	ShaderLibraryEntry::ShaderLibraryEntry(MemoryResource& resource) noexcept
		: Bytecode(resource)
	{
	}

	ShaderLibrarySlot::ShaderLibrarySlot(MemoryResource& resource) noexcept
		: Entries(resource)
	{
	}
}
