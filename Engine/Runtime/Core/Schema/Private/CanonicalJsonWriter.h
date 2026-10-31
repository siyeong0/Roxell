#pragma once

#include <CanonicalJsonCodec.h>

namespace rox::core
{
	// The caller has completed generic limits, registry validation and schema/default limits.
	// Value and registry storage remain immutable and alive through both preparation and emission.
	[[nodiscard]] Result<Array<Byte>> WriteCanonicalJsonBytes(const Value& value, const SchemaDescriptor& schema,
	                                                          const SchemaRegistry& registry,
	                                                          MemoryResource& outputResource,
	                                                          const JsonWriteLimits& limits);

	// Scratch is borrowed only for this call; the returned bytes use outputResource exclusively.
	[[nodiscard]] Result<Array<Byte>> WriteCanonicalJsonBytes(const Value& value, const SchemaDescriptor& schema,
	                                                          const SchemaRegistry& registry,
	                                                          MemoryResource& outputResource,
	                                                          const JsonWriteLimits& limits,
	                                                          MemoryResource& scratchResource);
} // namespace rox::core
