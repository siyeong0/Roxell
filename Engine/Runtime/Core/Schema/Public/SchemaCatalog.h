#pragma once

#include <Array.hpp>
#include <Result.hpp>
#include <Schema.h>
#include <SchemaRegistry.h>
#include <Span.hpp>
#include <Types.h>

namespace rox::core
{
	class MemoryResource;

	inline constexpr TypeId SCHEMA_CATALOG_TYPE_ID{0x4368726f6d612e53ull, 0x4348454d412e5631ull};
	inline constexpr uint32 SCHEMA_CATALOG_FORMAT_VERSION = 1;

	struct SchemaCatalogLimits final
	{
		static constexpr uint64 HARD_MAXIMUM_BYTES = 64ull * 1024ull * 1024ull;
		static constexpr uint64 HARD_MAXIMUM_SCHEMAS = 65'536;
		static constexpr uint64 HARD_MAXIMUM_FIELDS = 2'097'152;
		static constexpr uint64 HARD_MAXIMUM_VALUES = 4'194'304;
		static constexpr uint32 HARD_MAXIMUM_DEPTH = 64;
		static constexpr uint64 HARD_MAXIMUM_STRING_BYTES = 1ull * 1024ull * 1024ull;

		uint64 MaximumBytes = HARD_MAXIMUM_BYTES;
		uint64 MaximumSchemas = HARD_MAXIMUM_SCHEMAS;
		uint64 MaximumFields = HARD_MAXIMUM_FIELDS;
		uint64 MaximumValues = HARD_MAXIMUM_VALUES;
		uint32 MaximumDepth = HARD_MAXIMUM_DEPTH;
		uint64 MaximumStringBytes = HARD_MAXIMUM_STRING_BYTES;

		[[nodiscard]] constexpr bool IsValidProfile() const noexcept
		{
			return MaximumBytes >= 48 && MaximumBytes <= HARD_MAXIMUM_BYTES && MaximumSchemas <= HARD_MAXIMUM_SCHEMAS &&
			       MaximumFields <= HARD_MAXIMUM_FIELDS && MaximumValues != 0 && MaximumValues <= HARD_MAXIMUM_VALUES &&
			       MaximumDepth != 0 && MaximumDepth <= HARD_MAXIMUM_DEPTH && MaximumStringBytes != 0 &&
			       MaximumStringBytes <= HARD_MAXIMUM_STRING_BYTES;
		}
	};

	[[nodiscard]] Result<Array<Byte>> WriteSchemaCatalog(const SchemaRegistry& registry, MemoryResource& outputResource, MemoryResource& scratchResource, const SchemaCatalogLimits& limits = {});

	// Scratch storage is borrowed only for the call. This overload uses bootstrap output storage.
	[[nodiscard]] Result<SchemaRegistry> ReadSchemaCatalog(Span<const Byte> bytes, MemoryResource& scratchResource, const SchemaCatalogLimits& limits = {});
	// The output resource must outlive the returned registry and its owned metadata.
	[[nodiscard]] Result<SchemaRegistry> ReadSchemaCatalog(Span<const Byte> bytes, MemoryResource& outputResource, MemoryResource& scratchResource, const SchemaCatalogLimits& limits = {});

	// A distinct descriptor-transfer envelope: unchanged catalog bytes plus complete display metadata.
	// This is not the persisted schema-catalog format. ReadSchemaCatalog deliberately rejects it.
	[[nodiscard]] Result<Array<Byte>> WriteSchemaCatalogTransfer(const SchemaRegistry&, MemoryResource& outputResource, MemoryResource& scratchResource, const SchemaCatalogLimits& limits = {});
	// Input and scratch storage are borrowed only during the call; all metadata is owned by the result.
	[[nodiscard]] Result<SchemaRegistry> ReadSchemaCatalogTransfer(Span<const Byte>, MemoryResource& outputResource, MemoryResource& scratchResource, const SchemaCatalogLimits& limits = {});
} // namespace rox::core
