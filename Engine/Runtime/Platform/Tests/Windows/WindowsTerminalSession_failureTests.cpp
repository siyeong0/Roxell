#include <WindowsTerminalSession.h>
#include <Types.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <vector>
#include <string>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	std::string fixturePath()
	{
		wchar_t path[32768]{};
		const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
		if (length == 0 || length >= 32768)
		{
			return {};
		}
		std::wstring file(path, length);
		const rox::core::usize separator = file.find_last_of(L"\\/");
		if (separator == std::wstring::npos)
		{
			return {};
		}
		file.resize(separator + 1);
		file += L"Test-PlatformTerminalFixture.exe";
		const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, file.data(), static_cast<int>(file.size()),
			nullptr, 0, nullptr, nullptr);
		if (size <= 0)
		{
			return {};
		}
		std::string result(static_cast<rox::core::usize>(size), '\0');
		if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, file.data(), static_cast<int>(file.size()),
			result.data(), size, nullptr, nullptr) != size)
		{
			return {};
		}
		return result;
	}

	bool awaitClosed(os::ITerminalSession& session)
	{
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!session.IsClosed() && rox::core::MonotonicClock::now() < deadline)
		{
			rox::core::SleepFor(rox::core::Milliseconds(2));
		}
		return session.IsClosed();
	}

	bool awaitText(os::ITerminalSession& session, core::MemoryResource& memory, std::string* output, const char* expected)
	{
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::TerminalOutput> batch = session.Poll(memory);
			if (!batch || batch.Value().StreamFailure.Code != core::EErrorCode::None)
			{
				return false;
			}
			if (!batch.Value().Bytes.IsEmpty())
			{
				output->append(reinterpret_cast<const char*>(batch.Value().Bytes.Data()), batch.Value().Bytes.Size());
			}
			if (output->find(expected) != std::string::npos)
			{
				return true;
			}
			if (batch.Value().bClosed)
			{
				return false;
			}
			rox::core::SleepFor(rox::core::Milliseconds(2));
		}
		return false;
	}

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

	std::vector<HandleEntry> snapshotHandles()
	{
		using Query = LONG (NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
		const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		const Query query = reinterpret_cast<Query>(GetProcAddress(ntdll, "NtQueryInformationProcess"));
		std::vector<char> buffer(1024 * 1024);
		ULONG returned = 0;
		if (!query || query(GetCurrentProcess(), 51, buffer.data(), static_cast<ULONG>(buffer.size()), &returned) < 0)
		{
			return {};
		}
		const HandleSnapshot* snapshot = reinterpret_cast<const HandleSnapshot*>(buffer.data());
		return { snapshot->Entries, snapshot->Entries + snapshot->Count };
	}

	bool hasHandle(const std::vector<HandleEntry>& handles, const HandleEntry& target)
	{
		for (const HandleEntry& entry : handles)
		{
			if (entry.Value == target.Value && entry.Type == target.Type && entry.Access == target.Access)
			{
				return true;
			}
		}
		return false;
	}

	std::string objectText(const HANDLE handle, const ULONG informationClass)
	{
		using QueryObject = LONG (NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
		const QueryObject query = reinterpret_cast<QueryObject>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
		struct UnicodeName
		{
			USHORT Length;
			USHORT MaximumLength;
			wchar_t* Buffer;
		};
		alignas(void*) char buffer[2048]{};
		ULONG returned = 0;
		if (!query || query(handle, informationClass, buffer, sizeof(buffer), &returned) < 0)
		{
			return "<query failed>";
		}
		const UnicodeName* text = reinterpret_cast<const UnicodeName*>(buffer);
		if (!text->Buffer || text->Length == 0)
		{
			return {};
		}
		char utf8[1024]{};
		const int size = WideCharToMultiByte(CP_UTF8, 0, text->Buffer, text->Length / sizeof(wchar_t),
			utf8, sizeof(utf8), nullptr, nullptr);
		return size > 0 ? std::string(utf8, static_cast<rox::core::usize>(size)) : "<conversion failed>";
	}

	bool initializeTerminalRuntime(const std::string& executable)
	{
		os::TerminalDesc desc;
		desc.ExecutablePath = core::StringView(executable);
		core::Result<core::InterfaceOwner<os::ITerminalSession>> session =
			os::CreateWindowsTerminalSession(desc, core::SystemMemoryResource::Get());
		if (!session)
		{
			return false;
		}
		session.Value()->BeginClose();
		return awaitClosed(*session.Value());
	}

	TEST(WindowsTerminalCreationTests, EachAllocationFailureCleansUpAndEventuallySucceeds)
	{
		const std::string executable = fixturePath();
		ASSERT_FALSE(executable.empty());
		ASSERT_TRUE(initializeTerminalRuntime(executable));
		os::TerminalDesc desc;
		desc.ExecutablePath = core::StringView(executable);
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		DWORD handlesBefore = 0;
		ASSERT_NE(GetProcessHandleCount(GetCurrentProcess(), &handlesBefore), FALSE);
		bool bSucceeded = false;
		for (core::uint32 accepted = 0; accepted < 32 && !bSucceeded; ++accepted)
		{
			core::FailingMemoryResource failing(tracking, accepted);
			{
				core::Result<core::InterfaceOwner<os::ITerminalSession>> result = os::CreateWindowsTerminalSession(desc, failing);
				if (result)
				{
					result.Value()->BeginClose();
					ASSERT_TRUE(awaitClosed(*result.Value()));
					bSucceeded = true;
				}
				else
				{
					EXPECT_EQ(result.GetError().Code, core::EErrorCode::ExternalFailure) << accepted;
					EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL") << accepted;
				}
			}
			EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0) << accepted;
		}
		EXPECT_TRUE(bSucceeded);
		DWORD handlesAfter = 0;
		ASSERT_NE(GetProcessHandleCount(GetCurrentProcess(), &handlesAfter), FALSE);
		EXPECT_EQ(handlesAfter, handlesBefore);
	}

	TEST(WindowsTerminalCreationTests, MissingExecutableFailsWithoutLeakingAllocationsOrHandles)
	{
		const std::string executable = fixturePath();
		ASSERT_FALSE(executable.empty());
		ASSERT_TRUE(initializeTerminalRuntime(executable));
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		os::TerminalDesc desc;
		desc.ExecutablePath = "Z:\\roxell-terminal-does-not-exist.exe";
		const std::vector<HandleEntry> before = snapshotHandles();
		ASSERT_FALSE(before.empty());
		{
			const core::Result<core::InterfaceOwner<os::ITerminalSession>> result = os::CreateWindowsTerminalSession(desc, tracking);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.GetError().Code, core::EErrorCode::ExternalFailure);
			EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
		const std::vector<HandleEntry> after = snapshotHandles();
		ASSERT_FALSE(after.empty());
		core::uint32 semaphores = 0;
		core::uint32 mutants = 0;
		core::uint32 events = 0;
		core::uint32 registrations = 0;
		core::uint32 resources = 0;
		for (const HandleEntry& entry : after)
		{
			if (hasHandle(before, entry))
			{
				continue;
			}
			const std::string type = objectText(entry.Value, 2);
			if (type == "Semaphore" || type == "Mutant")
			{
				const std::string name = objectText(entry.Value, 1);
				EXPECT_NE(name.find("WilError_03"), std::string::npos) << name;
				if (type == "Semaphore")
				{
					++semaphores;
				}
				else
				{
					++mutants;
				}
			}
			else if (type == "Event")
			{
				EXPECT_TRUE(objectText(entry.Value, 1).empty());
				++events;
			}
			else if (type == "EtwRegistration")
			{
				++registrations;
			}
			else if (type == "File" && GetFileType(entry.Value) == FILE_TYPE_DISK)
			{
				const std::string name = objectText(entry.Value, 1);
				EXPECT_NE(name.find("KernelBase.dll.mui"), std::string::npos) << name;
				++resources;
			}
			else
			{
				ADD_FAILURE() << "Unexpected retained handle type " << type << " value " << entry.Value;
			}
		}
		EXPECT_LE(semaphores, 2);
		EXPECT_LE(mutants, 1);
		EXPECT_LE(events, 2);
		EXPECT_LE(registrations, 2);
		EXPECT_LE(resources, 1);
		{
			const core::Result<core::InterfaceOwner<os::ITerminalSession>> result = os::CreateWindowsTerminalSession(desc, tracking);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.GetError().Code, core::EErrorCode::ExternalFailure);
			EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
		const std::vector<HandleEntry> repeated = snapshotHandles();
		ASSERT_FALSE(repeated.empty());
		core::uint32 debugMutants = 0;
		core::uint32 runtimeThreads = 0;
		for (const HandleEntry& entry : repeated)
		{
			if (!hasHandle(after, entry))
			{
				const std::string type = objectText(entry.Value, 2);
				if (type == "Mutant")
				{
					const std::string name = objectText(entry.Value, 1);
					EXPECT_NE(name.find("DBWinMutex"), std::string::npos) << name;
					++debugMutants;
				}
				else if (type == "Thread")
				{
					EXPECT_EQ(GetProcessIdOfThread(entry.Value), GetCurrentProcessId());
					EXPECT_EQ(entry.Access, 0x00100840u);
					++runtimeThreads;
				}
				else
				{
					ADD_FAILURE() << "Unexpected retained handle type " << type << " value " << entry.Value;
				}
			}
		}
		EXPECT_LE(debugMutants, 1);
		EXPECT_LE(runtimeThreads, 1);
	}

	TEST(WindowsTerminalBoundsTests, SmallQueueAndOutputBuffersCloseUnderBackpressure)
	{
		const std::string executable = fixturePath();
		ASSERT_FALSE(executable.empty());
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		os::TerminalDesc desc;
		desc.ExecutablePath = core::StringView(executable);
		desc.MaximumOutputBytes = 128;
		desc.MaximumPollBytes = 17;
		desc.MaximumQueuedCommands = 1;
		{
			core::Result<core::InterfaceOwner<os::ITerminalSession>> result = os::CreateWindowsTerminalSession(desc, memory);
			ASSERT_TRUE(result);
			core::InterfaceOwner<os::ITerminalSession> session = core::Move(result.Value());
			std::string output;
			ASSERT_TRUE(awaitText(*session, memory, &output, "READY"));
			const core::StringView command = "flood\r";
			ASSERT_TRUE(session->Write({ reinterpret_cast<const core::Byte*>(command.Data()), command.Size() }));
			ASSERT_TRUE(awaitText(*session, memory, &output, "FLOODING"));
			bool bSaturated = false;
			for (core::uint32 index = 0; index < 4096 && !bSaturated; ++index)
			{
				const core::Result<void> resized = session->Resize({ 80 + index % 2, 24 });
				if (!resized)
				{
					EXPECT_EQ(resized.GetError().Code, core::EErrorCode::Conflict);
					EXPECT_EQ(resized.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
					bSaturated = true;
				}
			}
			ASSERT_TRUE(bSaturated);
			session->BeginClose();
			ASSERT_TRUE(awaitClosed(*session));
			bool bDrained = false;
			core::usize total = 0;
			for (core::uint32 index = 0; index < 128 && !bDrained; ++index)
			{
				core::Result<os::TerminalOutput> batch = session->Poll(memory);
				ASSERT_TRUE(batch);
				EXPECT_TRUE(batch.Value().bClosing);
				EXPECT_LE(batch.Value().Bytes.Size(), 17);
				total += batch.Value().Bytes.Size();
				bDrained = batch.Value().bClosed;
			}
			EXPECT_TRUE(bDrained);
			EXPECT_LE(total, 128);
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}
}
