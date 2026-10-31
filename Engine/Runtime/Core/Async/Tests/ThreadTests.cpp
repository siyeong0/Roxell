#include <SystemMemoryResource.h>
#include <Thread.hpp>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <utility>

using namespace rox::core;

namespace
{
	class RefusingBackend final : public IThreadBackend
	{
	public:
		NativeThreadStartResult Start(NativeThreadEntry&) noexcept override
		{
			return {{}, {EThreadStartStatus::NativeFailure, 19}};
		}

		void Join(NativeThread) noexcept override
		{
			ADD_FAILURE() << "A refused thread cannot be joined.";
		}

		ThreadId CurrentThreadId() const noexcept override
		{
			return {};
		}
	};

	class ImmediateBackend final : public IThreadBackend
	{
	public:
		NativeThreadStartResult Start(NativeThreadEntry& entry) noexcept override
		{
			entry.Function(entry.Context);
			return {{this, {7}}, {EThreadStartStatus::Started, 0}};
		}

		void Join(NativeThread thread) noexcept override
		{
			EXPECT_EQ(thread.Handle, this);
			EXPECT_EQ(thread.Id.Value, 7u);
			++JoinCount;
		}

		ThreadId CurrentThreadId() const noexcept override { return {}; }

		int32 JoinCount = 0;
	};

	struct Target final
	{
		int32* Destroyed;
		bool* Invoked;

		Target(int32& destroyed, bool& bInvoked) noexcept
			: Destroyed(&destroyed)
			, Invoked(&bInvoked)
		{
		}

		Target(Target&& other) noexcept
			: Destroyed(Exchange(other.Destroyed, nullptr))
			, Invoked(other.Invoked)
		{
		}

		~Target() noexcept
		{
			if (Destroyed != nullptr)
			{
				++*Destroyed;
			}
		}

		void operator()() noexcept
		{
			*Invoked = true;
		}
	};
}

TEST(ThreadTests, BackendRefusalConsumesTargetAndReleasesControl)
{
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	RefusingBackend backend;
	int32 destroyed = 0;
	bool bInvoked = false;
	{
		Thread thread(memory, backend);
		const ThreadStartResult result = thread.Start(Target(destroyed, bInvoked));
		EXPECT_EQ(result.Status, EThreadStartStatus::NativeFailure);
		EXPECT_EQ(result.NativeError, 19u);
		EXPECT_FALSE(thread.IsJoinable());
		EXPECT_EQ(thread.GetId().Value, 0u);
		EXPECT_FALSE(bInvoked);
		EXPECT_EQ(destroyed, 1);
	}
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ThreadTests, CompletedThreadRemainsJoinableAfterMoveUntilJoined)
{
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	ImmediateBackend backend;
	int32 destroyed = 0;
	bool bInvoked = false;
	{
		Thread original(memory, backend);
		const ThreadStartResult result = original.Start(Target(destroyed, bInvoked));
		EXPECT_EQ(result.Status, EThreadStartStatus::Started);
		EXPECT_TRUE(bInvoked);
		EXPECT_EQ(destroyed, 0);
		EXPECT_TRUE(original.IsJoinable());
		Thread moved(std::move(original));
		EXPECT_FALSE(original.IsJoinable());
		EXPECT_TRUE(moved.IsJoinable());
		EXPECT_EQ(moved.GetId().Value, 7u);
		if (moved.IsJoinable())
		{
			moved.Join();
		}
		if (original.IsJoinable())
		{
			original.Join();
		}
		EXPECT_FALSE(moved.IsJoinable());
		EXPECT_EQ(moved.GetId().Value, 0u);
		EXPECT_EQ(destroyed, 1);
		EXPECT_EQ(backend.JoinCount, 1);
	}
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
