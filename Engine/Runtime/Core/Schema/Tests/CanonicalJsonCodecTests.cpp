#include <CanonicalJsonCodec.h>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	constexpr FieldId FIELD_ID{3, 1};

	Span<const Byte> asBytes(const StringView text) noexcept
	{
		return {reinterpret_cast<const Byte*>(text.Data()), text.Size()};
	}
}

TEST(CanonicalJsonCodecTests, ReadsAndWritesSchemaBoundInteger)
{
	SchemaFieldDescriptor field;
	field.ID = FIELD_ID;
	field.Name = "count";
	field.ValueKind = ESchemaValueKind::UnsignedInteger;
	const SchemaDescriptor schema{{1, 1}, {2, 1}, 1, "Example", {&field, 1}};
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	{
		const Result<Value> decoded = ReadCanonicalJson(asBytes("{\"count\":7}"), schema, memory);
		ASSERT_TRUE(decoded);
		const Value::Object* const object = decoded.Value().GetObject();
		ASSERT_NE(object, nullptr);
		const Value* const count = object->Fields.Find(FIELD_ID);
		ASSERT_NE(count, nullptr);
		ASSERT_NE(count->GetUnsignedInteger(), nullptr);
		EXPECT_EQ(*count->GetUnsignedInteger(), 7u);
		const Result<Array<Byte>> encoded = WriteCanonicalJson(decoded.Value(), schema, memory);
		ASSERT_TRUE(encoded);
		const StringView text(reinterpret_cast<const char*>(encoded.Value().Data()), encoded.Value().Size());
		EXPECT_EQ(text, StringView("{\"count\":7}"));
	}
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(CanonicalJsonCodecTests, RejectsMalformedInputAndAllocationFailure)
{
	SchemaFieldDescriptor field;
	field.ID = FIELD_ID;
	field.Name = "count";
	field.ValueKind = ESchemaValueKind::UnsignedInteger;
	const SchemaDescriptor schema{{1, 1}, {2, 1}, 1, "Example", {&field, 1}};
	const StringView malformed("{\"count\":7");
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	{
		const Result<Value> rejected = ReadCanonicalJson(asBytes(malformed), schema, memory);
		EXPECT_FALSE(rejected);
		EXPECT_EQ(malformed, StringView("{\"count\":7"));
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
		FailingMemoryResource failing(memory, 0);
		const Result<Value> refused = ReadCanonicalJson(asBytes("{\"count\":7}"), schema, failing);
		ASSERT_FALSE(refused);
		EXPECT_EQ(refused.GetError().Code, EErrorCode::ExternalFailure);
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
	}
}

TEST(CanonicalJsonCodecTests, DecimalIntegerStringsRespectRangeAndCanonicalSyntax)
{
	SchemaFieldDescriptor field;
	field.ID = FIELD_ID;
	field.Name = "count";
	field.ValueKind = ESchemaValueKind::UnsignedInteger;
	const SchemaDescriptor schema{{1, 1}, {2, 1}, 1, "Example", {&field, 1}};
	MemoryResource& memory = SystemMemoryResource::Get();
	const Result<Value> maximum = ReadCanonicalJson(asBytes("{\"count\":\"18446744073709551615\"}"), schema, memory);
	ASSERT_TRUE(maximum);
	const Value* const count = maximum.Value().GetObject()->Fields.Find(FIELD_ID);
	ASSERT_NE(count, nullptr);
	ASSERT_NE(count->GetUnsignedInteger(), nullptr);
	EXPECT_EQ(*count->GetUnsignedInteger(), 18446744073709551615ull);
	const Result<Array<Byte>> written = WriteCanonicalJson(maximum.Value(), schema, memory);
	ASSERT_TRUE(written);
	EXPECT_EQ((StringView(reinterpret_cast<const char*>(written.Value().Data()), written.Value().Size())),
	          StringView("{\"count\":18446744073709551615}"));
	for (const StringView invalid : {StringView("{\"count\":\"18446744073709551616\"}"),
	                                 StringView("{\"count\":\"01\"}"), StringView("{\"count\":\"+1\"}"),
	                                 StringView("{\"count\":\"-0\"}"), StringView("{\"count\":\"1e2\"}")})
	{
		EXPECT_FALSE(ReadCanonicalJson(asBytes(invalid), schema, memory)) << invalid.Data();
	}
}

TEST(CanonicalJsonCodecTests, UnknownNestedValuesRoundTripAndMalformedInputsFail)
{
	const SchemaDescriptor schema{{1, 1}, {2, 1}, 1, "Example", {}};
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	{
		const StringView text("{\"future\":{\"items\":[\"01\",true,null]}}");
		const Result<Value> decoded = ReadCanonicalJson(asBytes(text), schema, memory);
		ASSERT_TRUE(decoded);
		const Result<Array<Byte>> encoded = WriteCanonicalJson(decoded.Value(), schema, memory);
		ASSERT_TRUE(encoded);
		EXPECT_EQ(StringView(reinterpret_cast<const char*>(encoded.Value().Data()), encoded.Value().Size()), text);
		for (const StringView malformed : {StringView("{\"future\":[1,]}"), StringView("{\"future\":true} trailing"),
		                                   StringView("{\"future\":\"unterminated}")})
		{
			EXPECT_FALSE(ReadCanonicalJson(asBytes(malformed), schema, memory));
		}
	}
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(CanonicalJsonCodecTests, ReadByteLimitIncludesExactBoundary)
{
	const SchemaDescriptor schema{{1, 1}, {2, 1}, 1, "Example", {}};
	const StringView text("{\"future\":[1,2]}");
	JsonReadLimits limits;
	limits.MaxBytes = text.Size();
	MemoryResource& memory = SystemMemoryResource::Get();
	EXPECT_TRUE(ReadCanonicalJson(asBytes(text), schema, memory, limits));
	--limits.MaxBytes;
	const Result<Value> rejected = ReadCanonicalJson(asBytes(text), schema, memory, limits);
	ASSERT_FALSE(rejected);
	EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-CORE-JSON-BYTE-LIMIT");
}

TEST(CanonicalJsonCodecTests, NestedReadAllocationFailuresReleasePartialValues)
{
	const SchemaDescriptor schema{{1, 1}, {2, 1}, 1, "Example", {}};
	const StringView text("{\"future\":{\"items\":[\"first\",\"second\",{\"inner\":true}]}}");
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	bool bSucceeded = false;
	for (usize allowedAttempts = 0; allowedAttempts < 128 && !bSucceeded; ++allowedAttempts)
	{
		FailingMemoryResource failing(tracked, allowedAttempts);
		{
			const Result<Value> result = ReadCanonicalJson(asBytes(text), schema, failing);
			bSucceeded = static_cast<bool>(result);
			if (!bSucceeded)
			{
				EXPECT_EQ(result.GetError().Code, EErrorCode::ExternalFailure) << allowedAttempts;
			}
		}
		EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0u) << allowedAttempts;
	}
	EXPECT_TRUE(bSucceeded);
}
