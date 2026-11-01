#include <Accessibility.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	TEST(AccessibilitySnapshotTests, RetainsTreeAndFindsNodesByCompleteIdentity)
	{
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		{
			core::Array<os::AccessibilityNode> nodes(memory);
			os::AccessibilityNode root(memory);
			root.Id = { 1, 2, 3, 0 };
			nodes.AddRequired(core::Move(root));
			os::AccessibilityNode child(memory);
			child.Id = { 1, 2, 3, 7 };
			child.Parent = { 1, 2, 3, 0 };
			child.Role = os::EAccessibilityRole::Button;
			nodes.AddRequired(core::Move(child));
			core::Result<os::AccessibilitySnapshot> result = os::AccessibilitySnapshot::Create(42, core::Move(nodes));
			ASSERT_TRUE(result);
			EXPECT_EQ(result.Value().GetPublication(), 42);
			EXPECT_EQ(result.Value().GetNodes().Size(), 2);
			const os::AccessibilityNode* found = result.Value().Find({ 1, 2, 3, 7 });
			ASSERT_NE(found, nullptr);
			EXPECT_EQ(found->Role, os::EAccessibilityRole::Button);
			EXPECT_EQ(result.Value().Find({ 1, 2, 4, 7 }), nullptr);
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(AccessibilitySnapshotTests, DuplicateIdentityIsRejectedWithoutLeakingNodes)
	{
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		{
			core::Array<os::AccessibilityNode> nodes(memory);
			os::AccessibilityNode root(memory);
			root.Id = { 1, 2, 3, 0 };
			nodes.AddRequired(core::Move(root));
			os::AccessibilityNode duplicate(memory);
			duplicate.Id = { 1, 2, 3, 0 };
			duplicate.Parent = { 1, 2, 3, 0 };
			nodes.AddRequired(core::Move(duplicate));
			const core::Result<os::AccessibilitySnapshot> result = os::AccessibilitySnapshot::Create(1, core::Move(nodes));
			ASSERT_FALSE(result);
			EXPECT_EQ(result.GetError().Code, core::EErrorCode::InvalidArgument);
			EXPECT_EQ(result.GetError().DiagnosticID, "Platform.Accessibility");
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(AccessibilityRequestTests, EmptyRequestCannotBeginAndCanBeCompletedSafely)
	{
		os::AccessibilityRequest request;
		EXPECT_FALSE(request.IsValid());
		EXPECT_FALSE(request.TryBegin());
		request.Complete(core::EErrorCode::None);
		EXPECT_FALSE(request.IsValid());
	}
}
