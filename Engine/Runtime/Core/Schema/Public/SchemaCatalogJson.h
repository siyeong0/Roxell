#pragma once

#include <Array.hpp>
#include <Result.hpp>
#include <Types.h>

namespace rox::core
{
	class MemoryResource;
	class SchemaRegistry;

	struct SchemaCatalogJsonLimits final
	{
		static constexpr uint64 HARD_MAXIMUM_BYTES = 8ull * 1024ull * 1024ull;
		static constexpr uint64 HARD_MAXIMUM_VALUES = 1'000'000;
		static constexpr uint32 HARD_MAXIMUM_DEPTH = 64;

		uint64 MaximumBytes = HARD_MAXIMUM_BYTES;
		uint64 MaximumValues = HARD_MAXIMUM_VALUES;
		uint32 MaximumDepth = HARD_MAXIMUM_DEPTH;

		[[nodiscard]] constexpr bool IsValidProfile() const noexcept
		{
			return MaximumBytes >= 64 && MaximumBytes <= HARD_MAXIMUM_BYTES && MaximumValues != 0 && MaximumValues <= HARD_MAXIMUM_VALUES && MaximumDepth != 0 && MaximumDepth <= HARD_MAXIMUM_DEPTH;
		}
	};

	[[nodiscard]] Result<Array<Byte>> WriteSchemaCatalogJson(const SchemaRegistry& registry, MemoryResource& resource, const SchemaCatalogJsonLimits& limits = {});
} // namespace rox::core
