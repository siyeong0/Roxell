#include <ObjectPool.hpp>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <cstdint>
#include <gtest/gtest.h>

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

TEST(ObjectPoolTests, Lifetime)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	{
		ObjectPool<Probe> pool(tracked);
		ASSERT_TRUE(pool.Initialize(2));
		Probe* const first = pool.Emplace(destroyed, 11);
		Probe* const second = pool.Emplace(destroyed, 22);
		ASSERT_TRUE(first != nullptr && second != nullptr);
		ASSERT_TRUE(reinterpret_cast<std::uintptr_t>(first) % 128 == 0);
		ASSERT_TRUE(reinterpret_cast<std::uintptr_t>(second) % 128 == 0);
		ASSERT_TRUE(pool.Emplace(destroyed, 33) == nullptr);
		ASSERT_TRUE(pool.Size() == 2 && second->m_Value == 22);
		pool.Destroy(*first);
		ASSERT_TRUE(destroyed == 1 && pool.Size() == 1 && second->m_Value == 22);
		Probe* const replacement = pool.Emplace(destroyed, 44);
		ASSERT_TRUE(replacement != nullptr && replacement->m_Value == 44);
		pool.Clear();
		ASSERT_TRUE(destroyed == 3 && pool.IsEmpty());
		pool.Clear();
		ASSERT_EQ(destroyed, 3);
	}
	ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
}

TEST(ObjectPoolTests, InitializationFailureCanBeRetriedWithoutLeaking)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	int32 destroyed = 0;
	{
		ObjectPool<Probe> pool(failing);
		EXPECT_FALSE(pool.Initialize(2));
		EXPECT_FALSE(pool.IsInitialized());
		EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
		failing.Reset(1);
		ASSERT_TRUE(pool.Initialize(2));
		EXPECT_EQ(pool.Capacity(), 2);
		ASSERT_NE(pool.Emplace(destroyed, 7), nullptr);
	}
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(ObjectPoolTests, ClearDestroysSparseObjectsAcrossBitmapWords)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	int32 destroyed = 0;
	{
		ObjectPool<Probe> pool(tracked);
		ASSERT_TRUE(pool.Initialize(130));
		Probe* objects[130]{};
		for (usize index = 0; index < 130; ++index)
		{
			objects[index] = pool.Emplace(destroyed, static_cast<int32>(index));
			ASSERT_NE(objects[index], nullptr);
		}
		for (usize index = 0; index < 130; ++index)
		{
			if (index != 0 && index != 64 && index != 129)
			{
				pool.Destroy(*objects[index]);
			}
		}
		EXPECT_EQ(pool.Size(), 3);
		EXPECT_EQ(destroyed, 127);
		pool.Clear();
		EXPECT_TRUE(pool.IsEmpty());
		EXPECT_EQ(destroyed, 130);
		EXPECT_NE(pool.Emplace(destroyed, 42), nullptr);
	}
	EXPECT_EQ(destroyed, 131);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}
