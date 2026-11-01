#include <Accessibility.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	class TestTextGeometry final : public os::IAccessibilityTextGeometry
	{
	public:
		explicit TestTextGeometry(bool* destroyed) noexcept
			: m_Destroyed(destroyed)
		{
		}

		~TestTextGeometry() override
		{
			*m_Destroyed = true;
		}

		core::Result<core::Array<os::AccessibilityRect>> GetBounds(core::uint32 first, core::uint32 last, core::MemoryResource& memory) const override
		{
			core::Array<os::AccessibilityRect> bounds(memory);
			if (first > last || last > 3)
			{
				return core::Result<core::Array<os::AccessibilityRect>>::Failure(core::EErrorCode::InvalidArgument, "Test.Geometry", "Invalid range.");
			}
			if (!bounds.Add({ static_cast<core::float64>(first * 10), 5, static_cast<core::float64>((last - first) * 10), 20 }))
			{
				return core::Result<core::Array<os::AccessibilityRect>>::Failure(core::EErrorCode::ExternalFailure, "Test.Geometry", "Allocation failed.");
			}
			return core::Result<core::Array<os::AccessibilityRect>>::Success(core::Move(bounds));
		}

		core::Result<core::uint32> HitTest(core::float64 x, core::float64) const override
		{
			return core::Result<core::uint32>::Success(x < 10 ? 0 : 1);
		}

		core::uint32 NextBoundary(core::uint32 byte, bool bForward) const noexcept override
		{
			return bForward ? (byte < 3 ? byte + 1 : 3) : (byte > 0 ? byte - 1 : 0);
		}

	private:
		bool* m_Destroyed;
	};

	TEST(AccessibilityGeometryTests, PublishedSnapshotRetainsGeometryUntilLastCopyIsReleased)
	{
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		bool bDestroyed = false;
		{
			core::Array<os::AccessibilityNode> nodes(memory);
			os::AccessibilityNode root(memory);
			root.Id = { 1, 1, 1, 0 };
			root.Role = os::EAccessibilityRole::Edit;
			ASSERT_TRUE(root.Value.Assign("abc"));
			root.Geometry = core::InterfaceOwner<os::IAccessibilityTextGeometry>::Create<TestTextGeometry>(memory, &bDestroyed);
			ASSERT_TRUE(root.Geometry);
			nodes.AddRequired(core::Move(root));
			core::Result<os::AccessibilitySnapshot> created = os::AccessibilitySnapshot::Create(1, core::Move(nodes));
			ASSERT_TRUE(created);
			os::AccessibilitySnapshot copy = created.Value();
			created = os::AccessibilitySnapshot::Create(2, core::Array<os::AccessibilityNode>(memory));
			ASSERT_TRUE(created);
			EXPECT_FALSE(bDestroyed);
			const os::AccessibilityNode* node = copy.Find({ 1, 1, 1, 0 });
			ASSERT_NE(node, nullptr);
			ASSERT_TRUE(node->Geometry);
			core::Result<core::Array<os::AccessibilityRect>> bounds = node->Geometry->GetBounds(1, 3, memory);
			ASSERT_TRUE(bounds);
			ASSERT_EQ(bounds.Value().Size(), 1);
			EXPECT_EQ(bounds.Value()[0].X, 10);
			EXPECT_EQ(bounds.Value()[0].Width, 20);
			const core::Result<core::uint32> hit = node->Geometry->HitTest(15, 5);
			ASSERT_TRUE(hit);
			EXPECT_EQ(hit.Value(), 1);
			EXPECT_EQ(node->Geometry->NextBoundary(1, true), 2);
			EXPECT_EQ(node->Geometry->NextBoundary(0, false), 0);
			const core::Result<core::Array<os::AccessibilityRect>> invalid = node->Geometry->GetBounds(3, 4, memory);
			ASSERT_FALSE(invalid);
			EXPECT_EQ(invalid.GetError().Code, core::EErrorCode::InvalidArgument);
		}
		EXPECT_TRUE(bDestroyed);
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}
}
