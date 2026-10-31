#include <SchemaCatalog.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(SchemaCatalogTests, RegisteredSchemaRoundTripsAndTruncationIsRejected)
{
	constexpr SchemaId SCHEMA_ID{11, 1};
	constexpr TypeId TYPE_ID{22, 1};
	constexpr FieldId FIELD_ID{33, 1};
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		SchemaFieldDescriptor field;
		field.ID = FIELD_ID;
		field.Name = "count";
		field.ValueKind = ESchemaValueKind::UnsignedInteger;
		const SchemaDescriptor descriptor{SCHEMA_ID, TYPE_ID, 1, "Example", {&field, 1}};
		SchemaRegistry registry(tracking);
		ASSERT_TRUE(registry.Register(descriptor));
		EXPECT_FALSE(registry.Register(descriptor));
		ASSERT_EQ(registry.GetSchemaCount(), 1u);
		registry.Freeze();

		const Result<Array<Byte>> written = WriteSchemaCatalog(registry, tracking, tracking);
		ASSERT_TRUE(written);
		ASSERT_GT(written.Value().Size(), 0u);
		const Span<const Byte> bytes = written.Value().View();
		const Result<SchemaRegistry> restored = ReadSchemaCatalog(bytes, tracking, tracking);
		ASSERT_TRUE(restored);
		EXPECT_TRUE(restored.Value().IsFrozen());
		ASSERT_EQ(restored.Value().GetSchemaCount(), 1u);
		const SchemaDescriptor* const schema = restored.Value().Find(TYPE_ID);
		ASSERT_NE(schema, nullptr);
		EXPECT_EQ(schema->ID, SCHEMA_ID);
		ASSERT_EQ(schema->Fields.size(), 1u);
		EXPECT_EQ(schema->Fields[0].ID, FIELD_ID);
		EXPECT_EQ(schema->Fields[0].Name, "count");
		EXPECT_EQ(schema->Fields[0].ValueKind, ESchemaValueKind::UnsignedInteger);
		const Result<SchemaRegistry> truncated = ReadSchemaCatalog(bytes.First(bytes.Size() - 1), tracking, tracking);
		EXPECT_FALSE(truncated);
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}
