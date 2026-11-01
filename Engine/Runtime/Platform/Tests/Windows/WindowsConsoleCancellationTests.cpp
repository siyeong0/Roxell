#include <WindowsConsoleCancellation.h>

#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	struct HandleEntry
	{
		HANDLE Value;
		ULONG_PTR HandleCount;
		ULONG_PTR PointerCount;
		ULONG Access;
		ULONG Type;
		ULONG Attributes;
		ULONG Reserved;
	};
	struct HandleSnapshot
	{
		ULONG_PTR Count;
		ULONG_PTR Reserved;
		HandleEntry Entries[1];
	};
	using QueryProcess = LONG (NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);

	std::vector<HandleEntry> getHandles()
	{
		const HMODULE module = GetModuleHandleW(L"ntdll.dll");
		const QueryProcess query = reinterpret_cast<QueryProcess>(GetProcAddress(module, "NtQueryInformationProcess"));
		std::vector<char> buffer(1024 * 1024);
		ULONG returned = 0;
		// ProcessHandleInformation exposes granted access so the retained owner handle can be distinguished
		// from unrelated handles created by runtime and tracing components.
		constexpr ULONG PROCESS_HANDLE_INFORMATION_CLASS = 51;
		if (!query || query(GetCurrentProcess(), PROCESS_HANDLE_INFORMATION_CLASS, buffer.data(), static_cast<ULONG>(buffer.size()), &returned) < 0)
		{
			return {};
		}
		const HandleSnapshot* snapshot = reinterpret_cast<const HandleSnapshot*>(buffer.data());
		return { snapshot->Entries, snapshot->Entries + snapshot->Count };
	}

	core::usize countThreadTerminationHandles(const std::vector<HandleEntry>& handles, const ULONG threadType)
	{
		core::usize count = 0;
		for (const HandleEntry& entry : handles)
		{
			if (entry.Access == THREAD_TERMINATE && entry.Type == threadType)
			{
				++count;
			}
		}
		return count;
	}

	TEST(WindowsConsoleCancellationTests, RejectsInvalidSourceWithoutClaimingSubscription)
	{
		core::FailingMemoryResource failing(core::SystemMemoryResource::Get(), 0);
		core::CancellationSource invalid = core::CancellationSource::Create(failing);
		ASSERT_FALSE(invalid.IsValid());
		core::CancellationSource valid;
		ASSERT_TRUE(valid.IsValid());
		os::WindowsConsoleCancellation subscription;
		const core::Result<void> rejected = subscription.Start(invalid);
		ASSERT_FALSE(rejected);
		EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-PLATFORM-CONSOLE-CANCELLATION-BUSY");
		EXPECT_TRUE(subscription.Start(valid));
		EXPECT_FALSE(valid.GetToken().IsCancellationRequested());
	}

	TEST(WindowsConsoleCancellationTests, RejectsConcurrentOwnerAndAllowsReacquisitionAfterDestruction)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		HANDLE sample = nullptr;
		ASSERT_TRUE(DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &sample, THREAD_TERMINATE, FALSE, 0));
		const std::vector<HandleEntry> sampleSnapshot = getHandles();
		ULONG threadType = 0;
		for (const HandleEntry& entry : sampleSnapshot)
		{
			if (entry.Value == sample)
			{
				threadType = entry.Type;
				break;
			}
		}
		ASSERT_TRUE(CloseHandle(sample));
		ASSERT_NE(threadType, 0u);
		const std::vector<HandleEntry> before = getHandles();
		ASSERT_FALSE(before.empty());
		const core::usize ownerHandlesBefore = countThreadTerminationHandles(before, threadType);
		{
			core::CancellationSource firstSource(tracking);
			core::CancellationSource secondSource(tracking);
			ASSERT_TRUE(firstSource.IsValid());
			ASSERT_TRUE(secondSource.IsValid());
			os::WindowsConsoleCancellation second;
			{
				os::WindowsConsoleCancellation first;
				ASSERT_TRUE(first.Start(firstSource));
				const std::vector<HandleEntry> active = getHandles();
				ASSERT_FALSE(active.empty());
				EXPECT_EQ(countThreadTerminationHandles(active, threadType), ownerHandlesBefore + 1);
				const core::Result<void> rejected = second.Start(secondSource);
				ASSERT_FALSE(rejected);
				EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::Conflict);
				EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-PLATFORM-CONSOLE-CANCELLATION-BUSY");
				EXPECT_FALSE(firstSource.GetToken().IsCancellationRequested());
				EXPECT_FALSE(secondSource.GetToken().IsCancellationRequested());
			}
			EXPECT_TRUE(second.Start(secondSource));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
		const std::vector<HandleEntry> after = getHandles();
		ASSERT_FALSE(after.empty());
		EXPECT_EQ(countThreadTerminationHandles(after, threadType), ownerHandlesBefore);
	}
} // namespace
