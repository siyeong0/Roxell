#include <NamePool.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <thread>

using namespace rox::core;

TEST(NamePoolTests, InternedNamesOwnAndRetainTheirBytes)
{
	NamePool first(SystemMemoryResource::Get());
	NamePool second(SystemMemoryResource::Get());
	ASSERT_TRUE(first.Intern({}).IsEmpty());
	ASSERT_EQ(first.Size(), 0);
	char input[] = "projectile";
	const Name name = first.Intern(input);
	input[0] = 'X';
	ASSERT_EQ(name.View(), "projectile");
	const char* address = name.View().Data();
	first.Reserve(512);
	ASSERT_EQ(first.Find("projectile"), name);
	ASSERT_EQ(name.View().Data(), address);
	ASSERT_EQ(first.Intern("projectile").View().Data(), address);
	ASSERT_TRUE(first.Find("missing").IsEmpty());
	const Name other = second.Intern("projectile");
	ASSERT_EQ(other, name);
	ASSERT_EQ(other.GetHash(), name.GetHash());
	ASSERT_TRUE(first.Intern("Projectile") != name);
}

TEST(NamePoolTests, SimultaneousInterningSharesOneStableName)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		NamePool pool(tracking);
		std::array<Name, 4> names{};
		std::array<std::thread, 4> workers;
		for (usize index = 0; index < workers.size(); ++index)
		{
			workers[index] = std::thread([&pool, &names, index]()
			{
				for (uint32 repeat = 0; repeat < 1000; ++repeat)
				{
					names[index] = pool.Intern("shared-projectile");
				}
			});
		}
		for (std::thread& worker : workers)
		{
			worker.join();
		}
		ASSERT_EQ(pool.Size(), 1);
		for (const Name name : names)
		{
			ASSERT_EQ(name.View(), "shared-projectile");
			ASSERT_EQ(name.View().Data(), names[0].View().Data());
		}
	}
	ASSERT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
}
