#pragma once

#include <Array.hpp>
#include <Span.hpp>
#include <Result.hpp>
#include <Schema.h>
#include <Types.h>
#include <Value.h>

#include <span>

namespace rox::core
{
	class MemoryResource;
	class SchemaRegistry;

	// Overloads accepting SchemaRegistry require a frozen registry and assert on lifecycle misuse.

	struct JsonReadLimits
	{
		uint64 MaxBytes = 16ull * 1024ull * 1024ull;
		uint32 MaxDepth = 64;
		uint64 MaxValues = 1'000'000;
	};

	struct JsonWriteLimits
	{
		uint64 MaxBytes = 16ull * 1024ull * 1024ull;
		uint32 MaxDepth = 64;
		uint64 MaxValues = 1'000'000;
	};

	[[nodiscard]] Result<Value> ReadCanonicalJson(rox::core::Span<const Byte> bytes, const SchemaDescriptor& schema, const JsonReadLimits& limits);
	[[nodiscard]] Result<Value> ReadCanonicalJson(rox::core::Span<const Byte> bytes, TypeId rootType, const SchemaRegistry& registry, const JsonReadLimits& limits);
	[[nodiscard]] Result<Value> ReadCanonicalJson(rox::core::Span<const Byte> bytes, const SchemaDescriptor& schema, MemoryResource& resource, const JsonReadLimits& limits = {});
	[[nodiscard]] Result<Value> ReadCanonicalJson(rox::core::Span<const Byte> bytes, TypeId rootType, const SchemaRegistry& registry, MemoryResource& resource, const JsonReadLimits& limits = {});

	[[nodiscard]] Result<Array<Byte>> WriteCanonicalJson(const Value& value, const SchemaDescriptor& schema, MemoryResource& resource, const JsonWriteLimits& limits = {});
	[[nodiscard]] Result<Array<Byte>> WriteCanonicalJson(const Value& value, TypeId rootType, const SchemaRegistry& registry, MemoryResource& resource, const JsonWriteLimits& limits = {});
} // namespace rox::core
