#include <Result.hpp>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

using namespace rox::core;

TEST(ResultTests, ResultRetainsSuccessAndFailureDetails)
{
	const Result<int32> success = Result<int32>::Success(42);
	ASSERT_TRUE(success.HasValue());
	ASSERT_EQ(success.Value(), 42);
	const Result<int32> failure = Result<int32>::Failure(EErrorCode::InvalidArgument, "TEST-ID", "Bad input", "Retry");
	ASSERT_FALSE(failure.HasValue());
	ASSERT_EQ(failure.GetError().Code, EErrorCode::InvalidArgument);
	ASSERT_EQ(failure.GetError().DiagnosticID, "TEST-ID");
	ASSERT_EQ(failure.GetError().Message, "Bad input");
	ASSERT_EQ(failure.GetError().RecoveryHint, "Retry");
	ASSERT_TRUE(Result<void>::Success().HasValue());
}

TEST(ResultTests, FailedResultCopyPreservesCodeWhenDiagnosticAllocationFails)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracking, std::numeric_limits<usize>::max());
	{
		constexpr StringView DIAGNOSTIC = "ROX-TEST-LONG-FAILURE-DIAGNOSTIC-IDENTIFIER";
		constexpr StringView MESSAGE = "The requested operation could not be completed.";
		Error source{EErrorCode::ExternalFailure, MakeLossyString(DIAGNOSTIC, failing),
		             MakeLossyString(MESSAGE, tracking), MakeLossyString("retry", tracking)};
		failing.Reset(0);
		const Result<int32> result = Result<int32>::Failure(source);
		ASSERT_FALSE(result.HasValue());
		ASSERT_EQ(result.GetError().Code, EErrorCode::ExternalFailure);
		ASSERT_TRUE(result.GetError().DiagnosticID.IsEmpty());
		ASSERT_EQ(result.GetError().Message, MESSAGE);
		ASSERT_EQ(result.GetError().RecoveryHint, "retry");
		ASSERT_EQ(source.DiagnosticID, DIAGNOSTIC);
		ASSERT_EQ(source.Message, MESSAGE);
		ASSERT_TRUE(failing.GetAllocationAttemptCount() > 0);
	}
	ASSERT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	ASSERT_EQ(tracking.GetStatistics().LiveByteCount, 0);
}

TEST(ResultTests, ValueCopyMoveAndVoidFailure)
{
	struct MoveOnly final
	{
		int32 Value;
		explicit MoveOnly(int32 value) noexcept : Value(value) {}
		MoveOnly(const MoveOnly&) = delete;
		MoveOnly(MoveOnly&& other) noexcept : Value(other.Value)
		{
			other.Value = -1;
		}
	};
	int32 input = 42;
	auto copied = Result<int32>::SuccessCopy(input);
	input = 9;
	ASSERT_TRUE(static_cast<bool>(copied));
	EXPECT_EQ(copied.Value(), 42);
	copied.Value() = 7;
	EXPECT_EQ(static_cast<const Result<int32>&>(copied).Value(), 7);
	MoveOnly source(17);
	auto moved = Result<MoveOnly>::Success(rox::core::Move(source));
	EXPECT_EQ(source.Value, -1);
	MoveOnly extracted(rox::core::Move(moved).TakeValue());
	EXPECT_EQ(extracted.Value, 17);

	const auto success = Result<void>::Success();
	EXPECT_TRUE(static_cast<bool>(success));
	success.Value();
	const auto failure = Result<void>::Failure(EErrorCode::NotFound, "missing", "Not found", "Retry");
	EXPECT_FALSE(static_cast<bool>(failure));
	EXPECT_EQ(failure.GetError().Code, EErrorCode::NotFound);
	EXPECT_EQ(failure.GetError().DiagnosticID, "missing");
	EXPECT_EQ(failure.GetError().RecoveryHint, "Retry");
}

TEST(ResultTests, ErrorAssignmentRetainsResourcesAndMoveEmptiesSource)
{
	TrackingMemoryResource first(SystemMemoryResource::Get());
	TrackingMemoryResource second(SystemMemoryResource::Get());
	{
		Error source{EErrorCode::Conflict, MakeLossyString("source-id", first),
		             MakeLossyString("source-message", first), MakeLossyString("source-hint", first)};
		Error destination{EErrorCode::Timeout, MakeLossyString("old-id", second),
		                  MakeLossyString("old-message", second), MakeLossyString("old-hint", second)};
		destination = source;
		EXPECT_EQ(destination.Code, EErrorCode::Conflict);
		EXPECT_EQ(destination.Message, "source-message");
		EXPECT_EQ(&destination.Message.GetResource(), &second);
		EXPECT_EQ(&source.Message.GetResource(), &first);
		destination = rox::core::Move(source);
		EXPECT_EQ(destination.DiagnosticID, "source-id");
		EXPECT_EQ(destination.RecoveryHint, "source-hint");
		EXPECT_EQ(&destination.DiagnosticID.GetResource(), &second);
		EXPECT_EQ(&destination.RecoveryHint.GetResource(), &second);
		EXPECT_EQ(source.Code, EErrorCode::Conflict);
		EXPECT_TRUE(source.DiagnosticID.IsEmpty());
		EXPECT_TRUE(source.Message.IsEmpty());
		EXPECT_TRUE(source.RecoveryHint.IsEmpty());
		EXPECT_EQ(&source.Message.GetResource(), &first);
	}
	EXPECT_EQ(first.GetStatistics().LiveAllocationCount, 0u);
	EXPECT_EQ(second.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ResultTests, SuccessCopySupportsAlignedCopyOnlyValues)
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
	struct alignas(64) CopyOnly final
	{
		int32 Value;
		explicit CopyOnly(const int32 value) noexcept : Value(value) {}
		CopyOnly(const CopyOnly&) noexcept = default;
		CopyOnly(CopyOnly&&) = delete;
	};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

	const CopyOnly input(37);
	const Result<CopyOnly> result = Result<CopyOnly>::SuccessCopy(input);
	ASSERT_TRUE(result.HasValue());
	EXPECT_EQ(result.Value().Value, 37);
	EXPECT_EQ(reinterpret_cast<std::uintptr_t>(&result.Value()) % alignof(CopyOnly), 0u);
}
