#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <ValueTraversal.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(ValueTraversalTests, VisitsArrayPathsAndHonorsStop)
{
	Value::Array items;
	items.AddRequired(Value(int64{4}));
	items.AddRequired(Value(int64{8}));
	const Value root(Move(items));
	uint32 calls = 0;
	bool bPathsValid = true;
	auto visitor = [&calls, &bPathsValid](const Value&, const Span<const ValuePathSegment> path) noexcept
	{
		if (calls == 0)
		{
			bPathsValid = bPathsValid && path.IsEmpty();
		}
		else
		{
			bPathsValid = bPathsValid && path.Size() == 1;
			if (path.Size() == 1)
			{
				bPathsValid = bPathsValid && path[0].Kind == EValuePathKind::ArrayElement && path[0].Index == calls - 1;
			}
		}
		++calls;
		return calls == 2 ? EValueVisitControl::Stop : EValueVisitControl::Continue;
	};
	const Result<ValueTraversalSummary> result = WalkValue(root, visitor);
	ASSERT_TRUE(result);
	EXPECT_TRUE(result.Value().Stopped);
	EXPECT_EQ(result.Value().VisitedCount, 2u);
	EXPECT_EQ(calls, 2u);
	EXPECT_TRUE(bPathsValid);
}

TEST(ValueTraversalTests, LimitsRejectWithoutChangingInput)
{
	Value::Array items;
	items.AddRequired(Value(int64{4}));
	const Value root(Move(items));
	auto visitor = [](const Value&, Span<const ValuePathSegment>) noexcept
	{
		return EValueVisitControl::Continue;
	};
	const Result<ValueTraversalSummary> depth = WalkValue(root, visitor, {0, 2});
	ASSERT_FALSE(depth);
	EXPECT_EQ(depth.GetError().DiagnosticID, "ROX-CORE-VALUE-WALK-DEPTH");
	const Result<ValueTraversalSummary> count = WalkValue(root, visitor, {1, 1});
	ASSERT_FALSE(count);
	EXPECT_EQ(count.GetError().DiagnosticID, "ROX-CORE-VALUE-WALK-COUNT");
	ASSERT_NE(root.GetArray(), nullptr);
	ASSERT_EQ(root.GetArray()->Size(), 1u);
	ASSERT_NE((*root.GetArray())[0].GetSignedInteger(), nullptr);
	EXPECT_EQ(*(*root.GetArray())[0].GetSignedInteger(), 4);
}

TEST(ValueTraversalTests, CloneFailurePreservesSourceAndReleasesMemory)
{
	const Value source("hello");
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		FailingMemoryResource failing(tracking, 1);
		const Result<Value> refused = CloneValue(source, failing);
		ASSERT_FALSE(refused);
		EXPECT_EQ(refused.GetError().Code, EErrorCode::ExternalFailure);
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		const Result<Value> copied = CloneValue(source, tracking);
		ASSERT_TRUE(copied);
		EXPECT_TRUE(copied.Value() == source);
		ASSERT_NE(copied.Value().GetString(), nullptr);
		ASSERT_NE(source.GetString(), nullptr);
		EXPECT_NE(copied.Value().GetString()->Data(), source.GetString()->Data());
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}
