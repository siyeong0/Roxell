#include <FailingMemoryResource.h>
#include <InterfaceOwner.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <utility>

using namespace rox::core;

namespace
{
	struct Interface
	{
		int32 Value = 17;
	};
	class Padding
	{
	public:
		virtual ~Padding() noexcept = default;

	public:
		int32 m_Value = 0;
	};
	class Concrete final : public Padding, public virtual Interface
	{
	public:
		explicit Concrete(int32& destroyed) noexcept
			: m_Destroyed(destroyed)
		{
		}
		~Concrete() noexcept override
		{
			++m_Destroyed;
		}
	private:
		int32& m_Destroyed;
	};

	class ReentrantConcrete final : public Interface
	{
	public:
		ReentrantConcrete(InterfaceOwner<Interface>* const owner, MemoryResource& resource, int32& destroyed,
			              bool& observedEmpty, const bool bInstallReplacement) noexcept
			: m_Owner(owner)
			, m_Resource(resource)
			, m_Destroyed(destroyed)
			, m_ObservedEmpty(observedEmpty)
			, m_bInstallReplacement(bInstallReplacement)
		{
		}
		~ReentrantConcrete() noexcept
		{
			++m_Destroyed;
			if (m_Owner != nullptr)
			{
				m_ObservedEmpty = !*m_Owner;
				m_Owner->Reset();
				if (m_bInstallReplacement)
				{
					*m_Owner = InterfaceOwner<Interface>::CreateRequired<ReentrantConcrete>(
						m_Resource, nullptr, m_Resource, m_Destroyed, m_ObservedEmpty, false);
				}
			}
		}

	private:
		InterfaceOwner<Interface>* m_Owner;
		MemoryResource& m_Resource;
		int32& m_Destroyed;
		bool& m_ObservedEmpty;
		bool m_bInstallReplacement;
	};
}

TEST(InterfaceOwnerTests, Ownership)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	int32 destroyed = 0;
	{
		InterfaceOwner<Concrete> concrete = InterfaceOwner<Concrete>::CreateRequired(tracked, destroyed);
		InterfaceOwner<Interface> owner(std::move(concrete));
		ASSERT_TRUE(!concrete && owner && owner->Value == 17 && owner.GetResource() == &tracked);
		Interface* const original = owner.Get();
		ASSERT_TRUE(!InterfaceOwner<Interface>::CreateInto<Concrete>(failing, &owner, destroyed));
		ASSERT_TRUE(owner.Get() == original && destroyed == 0);
		InterfaceOwner<const Interface> readOnly(std::move(owner));
		ASSERT_TRUE(!owner && readOnly && readOnly->Value == 17);
		readOnly.Reset();
		readOnly.Reset();
		ASSERT_EQ(destroyed, 1);
	}
	ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
}

TEST(InterfaceOwnerTests, ConvertedAssignmentReleasesOldCompleteObjectAndTransfersResource)
{
	TrackingMemoryResource firstResource(SystemMemoryResource::Get());
	TrackingMemoryResource secondResource(SystemMemoryResource::Get());
	FailingMemoryResource failing(firstResource, 0);
	int32 destroyed = 0;
	InterfaceOwner<Interface> empty = InterfaceOwner<Interface>::Create<Concrete>(failing, destroyed);
	EXPECT_FALSE(empty);
	EXPECT_EQ(empty.GetResource(), nullptr);
	InterfaceOwner<Interface> destination = InterfaceOwner<Interface>::Create<Concrete>(firstResource, destroyed);
	InterfaceOwner<Concrete> source = InterfaceOwner<Concrete>::Create(secondResource, destroyed);
	ASSERT_TRUE(destination);
	ASSERT_TRUE(source);
	Interface* const incomingView = source.Get();
	destination = std::move(source);
	EXPECT_FALSE(source);
	EXPECT_EQ(source.GetResource(), nullptr);
	EXPECT_EQ(destination.Get(), incomingView);
	EXPECT_EQ(destination.GetResource(), &secondResource);
	EXPECT_EQ((*destination).Value, 17);
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(firstResource.GetStatistics().LiveAllocationCount, 0);
	EXPECT_EQ(secondResource.GetStatistics().LiveAllocationCount, 1);
	destination.Reset();
	EXPECT_EQ(destroyed, 2);
	EXPECT_EQ(secondResource.GetStatistics().LiveAllocationCount, 0);
}

TEST(InterfaceOwnerTests, ResetDetachesBeforeDestructorAndKeepsReplacement)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	bool bObservedEmpty = false;
	InterfaceOwner<Interface> owner = InterfaceOwner<Interface>::CreateRequired<ReentrantConcrete>(
		tracked, nullptr, tracked, destroyed, bObservedEmpty, false);
	ASSERT_TRUE(InterfaceOwner<Interface>::CreateInto<ReentrantConcrete>(
		tracked, &owner, &owner, tracked, destroyed, bObservedEmpty, true));
	EXPECT_EQ(destroyed, 1);
	owner.Reset();
	EXPECT_TRUE(bObservedEmpty);
	EXPECT_TRUE(owner);
	EXPECT_EQ(destroyed, 2);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 1);
	owner.Reset();
	EXPECT_EQ(destroyed, 3);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(InterfaceOwnerTests, AssignmentPublishesIncomingOwnerBeforeDestructor)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	bool bObservedEmpty = true;
	InterfaceOwner<Interface> owner;
	ASSERT_TRUE(InterfaceOwner<Interface>::CreateInto<ReentrantConcrete>(
		tracked, &owner, &owner, tracked, destroyed, bObservedEmpty, false));
	InterfaceOwner<Concrete> incoming = InterfaceOwner<Concrete>::CreateRequired(tracked, destroyed);
	owner = std::move(incoming);
	EXPECT_FALSE(bObservedEmpty);
	EXPECT_FALSE(owner);
	EXPECT_FALSE(incoming);
	EXPECT_EQ(destroyed, 2);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}
