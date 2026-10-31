#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <Value.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(ValueTests, StringAllocationFailureReleasesPartialStorage)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	for (usize successfulAllocations = 0; successfulAllocations < 2; ++successfulAllocations)
	{
		FailingMemoryResource failing(tracking, successfulAllocations);
		const Result<Value> result = Value::CreateString("hello", failing);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, EErrorCode::ExternalFailure);
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
	}
}

TEST(ValueTests, StringOwnsCountedBytesAndMoveClearsSource)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		char bytes[] = {'a', '\0', static_cast<char>(0xff)};
		Result<Value> result = Value::CreateString(StringView(bytes, sizeof(bytes)), tracking);
		ASSERT_TRUE(result);
		Value value = Move(result).TakeValue();
		bytes[0] = 'z';
		Value moved = Move(value);
		EXPECT_EQ(value.GetKind(), ESchemaValueKind::Null);
		const StringView* text = moved.GetString();
		ASSERT_NE(text, nullptr);
		ASSERT_EQ(text->Size(), sizeof(bytes));
		EXPECT_EQ((*text)[0], 'a');
		EXPECT_EQ((*text)[1], '\0');
		EXPECT_EQ((*text)[2], static_cast<char>(0xff));
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ValueTests, ScalarKindsAndEqualityAreTypeSensitive)
{
	const Value nullValue;
	const Value boolean(true);
	const Value signedValue(int64{1});
	const Value unsignedValue(uint64{1});
	const Value floating(float64{1});
	EXPECT_EQ(nullValue.GetKind(), ESchemaValueKind::Null);
	EXPECT_EQ(nullValue.GetBoolean(), nullptr);
	ASSERT_NE(boolean.GetBoolean(), nullptr);
	EXPECT_TRUE(*boolean.GetBoolean());
	EXPECT_EQ(boolean.GetSignedInteger(), nullptr);
	EXPECT_EQ(*signedValue.GetSignedInteger(), 1);
	EXPECT_EQ(*unsignedValue.GetUnsignedInteger(), 1);
	EXPECT_EQ(*floating.GetFloatingPoint(), 1.0);
	EXPECT_FALSE(signedValue == unsignedValue);
	EXPECT_TRUE(unsignedValue == Value(uint64{1}));
}

TEST(ValueTests, ByteCreationFailureAndSharedCopyLifetime)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	Byte bytes[] = {Byte{1}, Byte{2}, Byte{3}};
	for (usize successfulAllocations = 0; successfulAllocations < 2; ++successfulAllocations)
	{
		FailingMemoryResource failing(tracked, successfulAllocations);
		const Result<Value> failed = Value::CreateBytes({bytes, 3}, failing);
		EXPECT_FALSE(failed);
		EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
	}
	{
		Result<Value> result = Value::CreateBytes({bytes, 3}, tracked);
		ASSERT_TRUE(result);
		Value original = Move(result).TakeValue();
		Value copy = original;
		bytes[0] = Byte{9};
		original = Value();
		ASSERT_NE(copy.GetBytes(), nullptr);
		ASSERT_EQ(copy.GetBytes()->Size(), 3);
		EXPECT_EQ((*copy.GetBytes())[0], Byte{1});
		EXPECT_EQ((*copy.GetBytes())[2], Byte{3});
	}
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}
