#include <FailingMemoryResource.h>
#include <MemoryBlock.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <utility>

using namespace rox::core;

namespace
{
	class ReentrantResource final : public MemoryResource
	{
	public:
		explicit ReentrantResource(MemoryResource& upstream) noexcept
			: m_Upstream(upstream)
		{
		}
		DiagnosticText GetName() const noexcept override
		{
			return "ReentrantResource";
		}
		EMemoryResourceThreadSafety GetThreadSafety() const noexcept override
		{
			return EMemoryResourceThreadSafety::CallerConfined;
		}

	protected:
		void* allocate(const MemoryAllocationRequest allocation) noexcept override
		{
			return m_Upstream.Allocate(allocation);
		}
		void deallocate(void* const memory, const MemoryAllocationRequest allocation) noexcept override
		{
			MemoryBlock* const owner = Owner;
			Owner = nullptr;
			if (owner != nullptr)
			{
				bObservedEmpty = owner->IsEmpty();
				owner->Reset();
				if (bInstallReplacement)
				{
					*owner = MemoryBlock::CreateRequired(m_Upstream, {24, 8});
				}
			}
			m_Upstream.Deallocate(memory, allocation);
		}

	public:
		MemoryBlock* Owner = nullptr;
		bool bObservedEmpty = false;
		bool bInstallReplacement = false;

	private:
		MemoryResource& m_Upstream;
	};
}

TEST(MemoryBlockTests, ResetDetachesBeforeCallbackAndKeepsCallbackReplacement)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	ReentrantResource resource(tracked);
	MemoryBlock owner = MemoryBlock::CreateRequired(resource, {16, 8});
	resource.Owner = &owner;
	resource.bInstallReplacement = true;
	owner.Reset();
	EXPECT_TRUE(resource.bObservedEmpty);
	EXPECT_EQ(owner.GetResource(), &tracked);
	EXPECT_EQ(owner.GetByteCount(), 24);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 1);
	owner.Reset();
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(MemoryBlockTests, AssignmentPublishesIncomingBlockBeforeReleaseCallback)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	ReentrantResource resource(tracked);
	MemoryBlock owner = MemoryBlock::CreateRequired(resource, {16, 8});
	MemoryBlock incoming = MemoryBlock::CreateRequired(tracked, {32, 8});
	resource.Owner = &owner;
	owner = std::move(incoming);
	EXPECT_FALSE(resource.bObservedEmpty);
	EXPECT_TRUE(owner.IsEmpty());
	EXPECT_TRUE(incoming.IsEmpty());
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(MemoryBlockTests, ReplacementAndMove)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	MemoryBlock block = MemoryBlock::CreateRequired(tracked, {24, 64});
	void* const original = block.GetData();
	ASSERT_FALSE(MemoryBlock::CreateInto(failing, &block, {40, 16}));
	ASSERT_TRUE(block.GetData() == original && block.GetByteCount() == 24);
	ASSERT_TRUE(block.GetResource() == &tracked);
	MemoryBlock moved(std::move(block));
	ASSERT_TRUE(block.IsEmpty() && block.GetResource() == nullptr);
	ASSERT_TRUE(moved.GetData() == original && moved.GetAlignment() == 64);
	moved.Reset();
	ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
	ASSERT_TRUE(MemoryBlock::CreateInto(failing, &moved, {0, 8}));
	ASSERT_TRUE(moved.IsEmpty() && failing.GetAllocationAttemptCount() == 1);
}

TEST(MemoryBlockTests, EmptyFailureAndSuccessfulReplacement)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	MemoryBlock empty;
	EXPECT_TRUE(empty.IsEmpty());
	EXPECT_EQ(empty.GetData(), nullptr);
	EXPECT_EQ(empty.GetResource(), nullptr);
	EXPECT_EQ(empty.GetByteCount(), 0);
	EXPECT_TRUE(MemoryBlock::Create(failing, {16, 8}).IsEmpty());

	MemoryBlock block = MemoryBlock::Create(tracked, {16, 8});
	ASSERT_FALSE(block.IsEmpty());
	ASSERT_TRUE(MemoryBlock::CreateInto(tracked, &block, {32, 16}));
	EXPECT_EQ(block.GetByteCount(), 32);
	EXPECT_EQ(block.GetAlignment(), 16);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 1);
	EXPECT_EQ(tracked.GetStatistics().LiveByteCount, 32);
	ASSERT_TRUE(MemoryBlock::CreateInto(tracked, &block, {0, 8}));
	EXPECT_TRUE(block.IsEmpty());
	EXPECT_EQ(block.GetResource(), nullptr);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(MemoryBlockTests, MoveAssignmentReleasesDestinationAndTransfersMetadata)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	MemoryBlock source = MemoryBlock::CreateRequired(tracked, {32, 32});
	MemoryBlock destination = MemoryBlock::CreateRequired(tracked, {16, 8});
	void* const data = source.GetData();
	destination = std::move(source);
	EXPECT_TRUE(source.IsEmpty());
	EXPECT_EQ(source.GetResource(), nullptr);
	EXPECT_EQ(destination.GetData(), data);
	EXPECT_EQ(destination.GetByteCount(), 32);
	EXPECT_EQ(destination.GetAlignment(), 32);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 1);
	destination.Reset();
	destination.Reset();
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}
