#include <WindowsThreadBackend.h>
#include <ClockPrimitives.hpp>
#include <gtest/gtest.h>
#include <semaphore>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

using namespace rox::core;
using namespace rox::os;

namespace
{
	struct Invocation final
	{
		WindowsThreadBackend Backend;
		ThreadId WorkerId;
		int32 Calls = 0;
	};

	void invoke(void* const context) noexcept
	{
		Invocation& invocation = *static_cast<Invocation*>(context);
		invocation.WorkerId = invocation.Backend.CurrentThreadId();
		++invocation.Calls;
	}

	void emptyEntry(void*) noexcept {}

	struct Rendezvous final
	{
		std::binary_semaphore Entered{ 0 };
		std::binary_semaphore Release{ 0 };
		int32 Published = 0;
	};

	void waitForRelease(void* const context) noexcept
	{
		Rendezvous& state = *static_cast<Rendezvous*>(context);
		state.Entered.release();
		state.Release.acquire();
		state.Published = 73;
	}

	struct SelfJoin final
	{
		WindowsThreadBackend Backend;
		NativeThread Thread;
		std::binary_semaphore Ready{ 0 };
	};

	void joinSelf(void* const context) noexcept
	{
		SelfJoin& state = *static_cast<SelfJoin*>(context);
		state.Ready.acquire();
		state.Backend.Join(state.Thread);
	}
}

TEST(WindowsThreadBackendTests, JoinPublishesExactlyOneInvocationAndWorkerIdentity)
{
	Invocation invocation;
	const ThreadId caller = invocation.Backend.CurrentThreadId();
	NativeThreadEntry entry{ invoke, &invocation };
	const NativeThreadStartResult started = invocation.Backend.Start(entry);
	ASSERT_EQ(started.Result.Status, EThreadStartStatus::Started);
	invocation.Backend.Join(started.Thread);
	EXPECT_EQ(started.Result.NativeError, 0);
	EXPECT_EQ(invocation.Calls, 1);
	EXPECT_EQ(invocation.WorkerId, started.Thread.Id);
	EXPECT_NE(invocation.WorkerId, caller);
	EXPECT_EQ(caller.Value, GetCurrentThreadId());
}

TEST(WindowsThreadBackendTests, JoinWaitsForWorkerAndPublishesItsWrites)
{
	WindowsThreadBackend backend;
	Rendezvous state;
	NativeThreadEntry entry{ waitForRelease, &state };
	const NativeThreadStartResult started = backend.Start(entry);
	ASSERT_EQ(started.Result.Status, EThreadStartStatus::Started);
	const bool bEntered = state.Entered.try_acquire_for(rox::core::Seconds(5));
	EXPECT_TRUE(bEntered);
	HANDLE observer = nullptr;
	const BOOL duplicated = DuplicateHandle(GetCurrentProcess(), started.Thread.Handle, GetCurrentProcess(), &observer, SYNCHRONIZE, FALSE, 0);
	if (!duplicated)
	{
		state.Release.release();
		backend.Join(started.Thread);
		FAIL() << "Failed to duplicate the worker handle.";
	}
	std::binary_semaphore joining{ 0 };
	std::binary_semaphore returned{ 0 };
	std::thread joiner([&]()
	{
		joining.release();
		backend.Join(started.Thread);
		returned.release();
	});
	const bool bJoining = joining.try_acquire_for(rox::core::Seconds(5));
	EXPECT_TRUE(bJoining);
	const bool bReturnedEarly = returned.try_acquire_for(rox::core::Milliseconds(100));
	state.Release.release();
	EXPECT_EQ(WaitForSingleObject(observer, 5000), WAIT_OBJECT_0);
	EXPECT_TRUE(CloseHandle(observer));
	joiner.join();
	EXPECT_FALSE(bReturnedEarly);
	EXPECT_EQ(state.Published, 73);
}

TEST(WindowsThreadBackendTests, RepeatedJoinsReleaseNativeHandles)
{
	WindowsThreadBackend backend;
	using CompareHandles = BOOL (WINAPI*)(HANDLE, HANDLE);
	const CompareHandles compareHandles = reinterpret_cast<CompareHandles>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CompareObjectHandles"));
	ASSERT_NE(compareHandles, nullptr);
	NativeThreadEntry entry{ emptyEntry, nullptr };
	const NativeThreadStartResult warmup = backend.Start(entry);
	ASSERT_EQ(warmup.Result.Status, EThreadStartStatus::Started);
	backend.Join(warmup.Thread);
	for (int32 index = 0; index < 64; ++index)
	{
		const NativeThreadStartResult started = backend.Start(entry);
		ASSERT_EQ(started.Result.Status, EThreadStartStatus::Started);
		HANDLE observer = nullptr;
		const BOOL duplicated = DuplicateHandle(GetCurrentProcess(), started.Thread.Handle, GetCurrentProcess(), &observer, SYNCHRONIZE, FALSE, 0);
		if (!duplicated)
		{
			backend.Join(started.Thread);
			FAIL() << "Failed to duplicate the thread handle.";
		}
		backend.Join(started.Thread);
		// A closed handle value can be reused; compare the retained kernel object instead.
		EXPECT_FALSE(compareHandles(started.Thread.Handle, observer));
		EXPECT_TRUE(CloseHandle(observer));
	}
}

TEST(WindowsThreadBackendTests, NullEntryTriggersContractAssertion)
{
	EXPECT_DEATH(
		{
			SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
			WindowsThreadBackend backend;
			NativeThreadEntry entry{};
			(void)backend.Start(entry);
		},
		"Native thread entry requires a function\\.");
}

TEST(WindowsThreadBackendTests, EmptyThreadTokenTriggersContractAssertion)
{
	EXPECT_DEATH(
		{
			SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
			WindowsThreadBackend backend;
			backend.Join({});
		},
		"Native thread join requires an owned handle and identifier\\.");
}

TEST(WindowsThreadBackendTests, SelfJoinTriggersContractAssertion)
{
	EXPECT_DEATH(
		{
			SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
			SelfJoin state;
			NativeThreadEntry entry{};
			entry.Function = joinSelf;
			entry.Context = &state;
			const NativeThreadStartResult started = state.Backend.Start(entry);
			if (started.Result.Status != EThreadStartStatus::Started)
			{
				ExitProcess(0);
			}
			state.Thread = started.Thread;
			state.Ready.release();
			WaitForSingleObject(started.Thread.Handle, 5000);
			ExitProcess(0);
		},
		"A native thread cannot join itself\\.");
}
