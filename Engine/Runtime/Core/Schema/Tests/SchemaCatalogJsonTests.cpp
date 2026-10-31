#include <FailingMemoryResource.h>
#include <SchemaCatalogJson.h>
#include <SchemaRegistry.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(SchemaCatalogJsonTests, EmptyCatalogHasCanonicalOutput)
{
	SchemaRegistry registry;
	registry.Freeze();
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	{
		const Result<Array<Byte>> result = WriteSchemaCatalogJson(registry, memory);
		ASSERT_TRUE(result);
		const StringView text(reinterpret_cast<const char*>(result.Value().Data()), result.Value().Size());
		EXPECT_EQ(text, StringView("{\"format\":\"roxell.schema-catalog\",\"version\":1,\"schemas\":[]}"));
	}
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(SchemaCatalogJsonTests, InvalidLimitsAndAllocationFailurePreserveRegistry)
{
	SchemaRegistry registry;
	registry.Freeze();
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	SchemaCatalogJsonLimits limits;
	limits.MaximumBytes = 63;
	const Result<Array<Byte>> invalid = WriteSchemaCatalogJson(registry, memory, limits);
	ASSERT_FALSE(invalid);
	EXPECT_EQ(invalid.GetError().Code, EErrorCode::InvalidArgument);
	FailingMemoryResource failing(memory, 0);
	const Result<Array<Byte>> refused = WriteSchemaCatalogJson(registry, failing);
	ASSERT_FALSE(refused);
	EXPECT_EQ(refused.GetError().Code, EErrorCode::ExternalFailure);
	EXPECT_TRUE(registry.IsFrozen());
	EXPECT_EQ(registry.GetSchemaCount(), 0u);
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
