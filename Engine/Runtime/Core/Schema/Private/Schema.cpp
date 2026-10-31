#include <Schema.h>

namespace rox::core
{
	const SchemaEnumEntry* FindSchemaEnumEntry(const SchemaFieldDescriptor& field, uint64 bits) noexcept
	{
		for (const SchemaEnumEntry& entry : field.EnumEntries)
		{
			if (entry.Bits == bits)
			{
				return &entry;
			}
		}

		return nullptr;
	}

	const SchemaEnumEntry* FindSchemaEnumEntry(const SchemaFieldDescriptor& field, rox::core::StringView name) noexcept
	{
		for (const SchemaEnumEntry& entry : field.EnumEntries)
		{
			if (entry.Name == name)
			{
				return &entry;
			}
		}

		return nullptr;
	}
}
