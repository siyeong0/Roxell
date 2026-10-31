#pragma once

#include <Array.hpp>
#include <CoreString.hpp>
#include <Result.hpp>
#include <Schema.h>
#include <UniquePtr.hpp>

namespace rox::core
{
	struct OwnedSchemaEnum final
	{
		explicit OwnedSchemaEnum(MemoryResource& resource) noexcept
		    : Names(resource)
		    , Entries(resource)
		{
		}

		Array<String> Names;
		Array<SchemaEnumEntry> Entries;
	};

	Result<void> ValidateSchemaEnumDescriptor(const SchemaFieldDescriptor& field);
	Result<void> ValidateSchemaEnumValue(const SchemaFieldDescriptor& field, const Value& value);
	Result<UniquePtr<OwnedSchemaEnum>> CopySchemaEnum(const SchemaFieldDescriptor& field, MemoryResource& resource);
}
