#include <FailingMemoryResource.h>
#include <SharedPtr.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <utility>

using namespace rox::core;

namespace
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
	class alignas(128) Probe final
	{
	public:
		Probe(int32& destroyed, const int32 value) noexcept
			: m_Destroyed(destroyed)
			, m_Value(value)
		{
		}
		~Probe() noexcept
		{
			++m_Destroyed;
		}

	public:
		int32& m_Destroyed;
		int32 m_Value;
	};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

TEST(SharedPtrTests, LifetimeAndFailure)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	int32 destroyed = 0;
	{
		SharedPtr<Probe> first = SharedPtr<Probe>::CreateRequired(tracked, destroyed, 9);
		SharedPtr<Probe> second = first;
		ASSERT_TRUE(first.UseCount() == 2 && second.Get() == first.Get());
		ASSERT_TRUE(!SharedPtr<Probe>::CreateInto(failing, &first, destroyed, 10));
		ASSERT_TRUE(first->m_Value == 9 && first.UseCount() == 2);
		first.Reset();
		ASSERT_TRUE(!first && second.UseCount() == 1 && destroyed == 0);
		second.Reset();
		ASSERT_EQ(destroyed, 1);
	}
	ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
}

TEST(SharedPtrTests, AssignmentMovesAndFactoriesPreserveOwnership)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	int32 destroyed = 0;
	SharedPtr<Probe> empty;
	EXPECT_EQ(empty, nullptr);
	EXPECT_EQ(empty.Get(), nullptr);
	EXPECT_EQ(empty.GetResource(), nullptr);
	EXPECT_EQ(empty.UseCount(), 0);
	EXPECT_FALSE(MakeShared<Probe>(failing, destroyed, 1));

	SharedPtr<Probe> first = MakeSharedRequired<Probe>(tracked, destroyed, 2);
	ASSERT_EQ(reinterpret_cast<uintptr>(first.Get()) % alignof(Probe), 0);
	EXPECT_EQ(first.GetResource(), &tracked);
	SharedPtr<Probe> second = MakeShared<Probe>(tracked, destroyed, 3);
	ASSERT_TRUE(second);
	second = first;
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(first.UseCount(), 2);
	EXPECT_EQ(second, first);
	EXPECT_EQ((*second).m_Value, 2);
	SharedPtr<Probe> moved(std::move(second));
	EXPECT_FALSE(second);
	EXPECT_EQ(moved.UseCount(), 2);
	first = std::move(moved);
	EXPECT_FALSE(moved);
	EXPECT_EQ(first.UseCount(), 1);
	ASSERT_TRUE(MakeSharedInto<Probe>(tracked, &first, destroyed, 4));
	EXPECT_EQ(destroyed, 2);
	EXPECT_EQ(first->m_Value, 4);
	first.Reset();
	EXPECT_EQ(destroyed, 3);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

namespace
{
	struct ResetOwnerOnDestruction
	{
		SharedPtr<ResetOwnerOnDestruction>* Owner;
		int32* Destroyed;

		ResetOwnerOnDestruction(SharedPtr<ResetOwnerOnDestruction>* owner, int32* destroyed) noexcept
			: Owner(owner)
			, Destroyed(destroyed)
		{
		}

		~ResetOwnerOnDestruction() noexcept
		{
			++*Destroyed;
			if (Owner != nullptr)
			{
				Owner->Reset();
			}
		}
	};
}

TEST(SharedPtrTests, ReplacementIsPublishedBeforeOldObjectDestruction)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	SharedPtr<ResetOwnerOnDestruction> owner;
	ASSERT_TRUE(SharedPtr<ResetOwnerOnDestruction>::CreateInto(tracked, &owner, &owner, &destroyed));
	ASSERT_TRUE(SharedPtr<ResetOwnerOnDestruction>::CreateInto(tracked, &owner, nullptr, &destroyed));
	EXPECT_FALSE(owner);
	EXPECT_EQ(destroyed, 2);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(SharedPtrTests, ResetDetachesBeforeObjectDestruction)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	SharedPtr<ResetOwnerOnDestruction> owner;
	ASSERT_TRUE(SharedPtr<ResetOwnerOnDestruction>::CreateInto(tracked, &owner, &owner, &destroyed));
	owner.Reset();
	EXPECT_FALSE(owner);
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(SharedPtrTests, SelfAssignmentKeepsOwnership)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	{
		SharedPtr<Probe> owner = MakeSharedRequired<Probe>(tracked, destroyed, 42);
		Probe* const original = owner.Get();
		SharedPtr<Probe>* const alias = &owner;
		owner = *alias;
		EXPECT_EQ(owner.Get(), original);
		EXPECT_EQ(owner.UseCount(), 1);
		owner = std::move(*alias);
		EXPECT_EQ(owner.Get(), original);
		EXPECT_EQ(owner.GetResource(), &tracked);
		EXPECT_EQ(owner.UseCount(), 1);
		EXPECT_EQ(destroyed, 0);
	}
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(SharedPtrTests, AssignmentBetweenResourcesRetainsOldObjectUntilLastOwner)
{
	TrackingMemoryResource firstResource(SystemMemoryResource::Get());
	TrackingMemoryResource secondResource(SystemMemoryResource::Get());
	int32 destroyed = 0;
	SharedPtr<Probe> destination = MakeSharedRequired<Probe>(firstResource, destroyed, 1);
	SharedPtr<Probe> oldCopy = destination;
	SharedPtr<Probe> source = MakeSharedRequired<Probe>(secondResource, destroyed, 2);
	destination = source;
	EXPECT_EQ(destination.GetResource(), &secondResource);
	EXPECT_EQ(destination.UseCount(), 2);
	EXPECT_EQ(oldCopy.UseCount(), 1);
	EXPECT_EQ(oldCopy->m_Value, 1);
	EXPECT_EQ(destroyed, 0);
	oldCopy.Reset();
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(firstResource.GetStatistics().LiveAllocationCount, 0);
	source.Reset();
	EXPECT_EQ(destination.UseCount(), 1);
	EXPECT_EQ(destination->m_Value, 2);
	destination.Reset();
	EXPECT_EQ(destroyed, 2);
	EXPECT_EQ(secondResource.GetStatistics().LiveAllocationCount, 0);
}
