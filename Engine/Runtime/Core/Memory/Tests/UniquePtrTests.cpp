#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <UniquePtr.hpp>

#include <gtest/gtest.h>
#include <new>
#include <utility>

using namespace rox::core;

namespace
{
	class ThrowingObject final
	{
	public:
		ThrowingObject(int32& destroyed, const bool bThrow)
			: m_Destroyed(destroyed)
		{
			if (bThrow)
			{
				throw std::bad_alloc();
			}
		}
		~ThrowingObject() noexcept
		{
			++m_Destroyed;
		}

	private:
		int32& m_Destroyed;
	};

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

TEST(UniquePtrTests, LifetimeAndFailure)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	int32 destroyed = 0;
	{
		UniquePtr<Probe> unique = UniquePtr<Probe>::CreateRequired(tracked, destroyed, 7);
		Probe* const original = unique.Get();
		ASSERT_TRUE(!UniquePtr<Probe>::CreateInto(failing, &unique, destroyed, 8));
		ASSERT_TRUE(unique.Get() == original && unique->m_Value == 7 && destroyed == 0);
		UniquePtr<Probe> moved(std::move(unique));
		ASSERT_TRUE(!unique && unique.GetResource() == nullptr && moved.Get() == original);
		moved.Reset();
		ASSERT_EQ(destroyed, 1);
	}

	ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
}

TEST(UniquePtrTests, ThrowingConstructionRollsBack)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	{
		UniquePtr<ThrowingObject> owner;
		ASSERT_TRUE(UniquePtr<ThrowingObject>::ConstructInto(tracked, &owner, destroyed, false));
		ThrowingObject* const original = owner.Get();
		bool bCaught = false;
		try
		{
			(void)UniquePtr<ThrowingObject>::ConstructInto(tracked, &owner, destroyed, true);
		}
		catch (const std::bad_alloc&)
		{
			bCaught = true;
		}
		ASSERT_TRUE(bCaught && owner.Get() == original && destroyed == 0);
		ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 1);
	}
	ASSERT_TRUE(destroyed == 1 && tracked.GetStatistics().LiveAllocationCount == 0);
}

namespace
{
	struct ResetOwnerOnDestruction final
	{
		UniquePtr<ResetOwnerOnDestruction>* Owner;
		int32* Destroyed;

		ResetOwnerOnDestruction(UniquePtr<ResetOwnerOnDestruction>* owner, int32* destroyed) noexcept
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

TEST(UniquePtrTests, ReplacementIsPublishedBeforeOldObjectDestruction)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	UniquePtr<ResetOwnerOnDestruction> owner;
	ASSERT_TRUE(UniquePtr<ResetOwnerOnDestruction>::CreateInto(tracked, &owner, &owner, &destroyed));
	ASSERT_TRUE(UniquePtr<ResetOwnerOnDestruction>::CreateInto(tracked, &owner, nullptr, &destroyed));
	EXPECT_FALSE(owner);
	EXPECT_EQ(destroyed, 2);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(UniquePtrTests, ResetDetachesBeforeObjectDestruction)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	UniquePtr<ResetOwnerOnDestruction> owner;
	ASSERT_TRUE(UniquePtr<ResetOwnerOnDestruction>::CreateInto(tracked, &owner, &owner, &destroyed));
	owner.Reset();
	EXPECT_FALSE(owner);
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(UniquePtrTests, SelfMoveKeepsOwnership)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	{
		UniquePtr<Probe> owner = MakeUniqueRequired<Probe>(tracked, destroyed, 42);
		Probe* const original = owner.Get();
		UniquePtr<Probe>* const alias = &owner;
		owner = std::move(*alias);
		EXPECT_EQ(owner.Get(), original);
		EXPECT_EQ(owner.GetResource(), &tracked);
		EXPECT_EQ(owner->m_Value, 42);
		EXPECT_EQ(destroyed, 0);
	}
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(UniquePtrTests, ThrowingFactoryAllocationFailurePreservesExistingOwner)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	int32 destroyed = 0;
	UniquePtr<ThrowingObject> owner;
	ASSERT_TRUE(ConstructUniqueInto<ThrowingObject>(tracked, &owner, destroyed, false));
	ThrowingObject* const original = owner.Get();
	EXPECT_FALSE(ConstructUniqueInto<ThrowingObject>(failing, &owner, destroyed, true));
	EXPECT_EQ(owner.Get(), original);
	EXPECT_EQ(owner.GetResource(), &tracked);
	EXPECT_EQ(destroyed, 0);
	owner.Reset();
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(UniquePtrTests, RawObjectHelpersHandleFailureAndDestroy)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	int32 destroyed = 0;
	Probe* const unexpected = CreateObject<Probe>(failing, destroyed, 7);
	EXPECT_EQ(unexpected, nullptr);
	DestroyObject(failing, unexpected);
	DestroyObject<Probe>(tracked, nullptr);
	Probe* const object = CreateObjectRequired<Probe>(tracked, destroyed, 9);
	ASSERT_NE(object, nullptr);
	EXPECT_EQ(object->m_Value, 9);
	DestroyObject(tracked, object);
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(UniquePtrTests, RelocationTransfersOwnershipWithoutDestroyingObject)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	alignas(UniquePtr<Probe>) Byte sourceStorage[sizeof(UniquePtr<Probe>)];
	alignas(UniquePtr<Probe>) Byte destinationStorage[sizeof(UniquePtr<Probe>)];
	UniquePtr<Probe>* const source = reinterpret_cast<UniquePtr<Probe>*>(sourceStorage);
	UniquePtr<Probe>* const destination = reinterpret_cast<UniquePtr<Probe>*>(destinationStorage);
	ConstructAt(source, MakeUniqueRequired<Probe>(tracked, destroyed, 23));
	Probe* const original = source->Get();
	RelocateAt(destination, source);
	EXPECT_EQ(destination->Get(), original);
	EXPECT_EQ(destination->GetResource(), &tracked);
	EXPECT_EQ(destroyed, 0);
	DestroyAt(destination);
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}
