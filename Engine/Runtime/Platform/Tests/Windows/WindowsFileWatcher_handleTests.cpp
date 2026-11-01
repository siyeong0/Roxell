#include <WindowsFileWatcher.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
		constexpr ULONG PROCESS_HANDLE_INFORMATION_CLASS = 51;
		if (!query || query(GetCurrentProcess(), PROCESS_HANDLE_INFORMATION_CLASS, buffer.data(), static_cast<ULONG>(buffer.size()), &returned) < 0)
		{
			return {};
		}
		const HandleSnapshot* snapshot = reinterpret_cast<const HandleSnapshot*>(buffer.data());
		return { snapshot->Entries, snapshot->Entries + snapshot->Count };
	}

	core::usize countFileHandles(const std::vector<HandleEntry>& handles, const ULONG fileType, const BY_HANDLE_FILE_INFORMATION& identity)
	{
		core::usize count = 0;
		for (const HandleEntry& entry : handles)
		{
			if (entry.Type != fileType || GetFileType(entry.Value) != FILE_TYPE_DISK)
			{
				continue;
			}
			BY_HANDLE_FILE_INFORMATION file{};
			if (GetFileInformationByHandle(entry.Value, &file) &&
				file.dwVolumeSerialNumber == identity.dwVolumeSerialNumber &&
				file.nFileIndexHigh == identity.nFileIndexHigh &&
				file.nFileIndexLow == identity.nFileIndexLow)
			{
				++count;
			}
		}
		return count;
	}

	bool hasHandle(const std::vector<HandleEntry>& handles, const HANDLE value, const ULONG type, const ULONG access)
	{
		for (const HandleEntry& entry : handles)
		{
			if (entry.Value == value && entry.Type == type && entry.Access == access)
			{
				return true;
			}
		}
		return false;
	}

	struct EventInformation
	{
		LONG Type;
		LONG State;
	};

	bool queryEvent(const HANDLE handle, EventInformation* const outInformation)
	{
		using QueryEvent = LONG (NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
		const HMODULE module = GetModuleHandleW(L"ntdll.dll");
		const QueryEvent query = reinterpret_cast<QueryEvent>(GetProcAddress(module, "NtQueryEvent"));
		ULONG returned = 0;
		return query && query(handle, 0, outInformation, sizeof(*outInformation), &returned) >= 0;
	}

	TEST(WindowsFileWatcherHandleTests, RepeatedPendingDestructionClosesDirectoryAndEventHandles)
	{
		const rox::core::MonotonicClock::duration::rep nonce = rox::core::MonotonicClock::now().time_since_epoch().count();
		const std::filesystem::path root = std::filesystem::temp_directory_path() / ("RoxellWatchHandles-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(nonce));
		struct Cleanup final
		{
			std::filesystem::path Path;
			~Cleanup()
			{
				std::error_code ignored;
				std::filesystem::remove_all(Path, ignored);
			}
		} cleanup{ root };
		ASSERT_TRUE(std::filesystem::create_directories(root));
		const std::u8string encoded = root.u8string();
		const std::string rootText(reinterpret_cast<const char*>(encoded.data()), encoded.size());
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			core::Result<os::WindowsFileWatcher> warmup = os::WindowsFileWatcher::Create(tracking, { rootText.data(), rootText.size() });
			ASSERT_TRUE(warmup);
			std::ofstream file(root / "warmup.txt");
			ASSERT_TRUE(file.good());
			file << "pending";
		}
		const std::vector<HandleEntry> before = getHandles();
		ASSERT_FALSE(before.empty());
		HANDLE sample = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		ASSERT_NE(sample, INVALID_HANDLE_VALUE);
		struct CloseSample final
		{
			HANDLE Value;
			~CloseSample()
			{
				if (Value != INVALID_HANDLE_VALUE)
				{
					CloseHandle(Value);
				}
			}
		} sampleGuard{ sample };
		BY_HANDLE_FILE_INFORMATION identity{};
		ASSERT_TRUE(GetFileInformationByHandle(sample, &identity));
		const std::vector<HandleEntry> during = getHandles();
		ASSERT_FALSE(during.empty());
		ULONG directoryType = 0;
		for (const HandleEntry& entry : during)
		{
			if (entry.Value == sample)
			{
				directoryType = entry.Type;
				break;
			}
		}
		ASSERT_NE(directoryType, 0u);
		const core::usize baseline = countFileHandles(before, directoryType, identity);
		ASSERT_EQ(countFileHandles(during, directoryType, identity), baseline + 1);
		ASSERT_TRUE(CloseHandle(sample));
		sampleGuard.Value = INVALID_HANDLE_VALUE;
		HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		ASSERT_NE(event, nullptr);
		const std::vector<HandleEntry> withSampleEvent = getHandles();
		ULONG eventType = 0;
		ULONG eventAccess = 0;
		for (const HandleEntry& entry : withSampleEvent)
		{
			if (entry.Value == event)
			{
				eventType = entry.Type;
				eventAccess = entry.Access;
				break;
			}
		}
		ASSERT_TRUE(CloseHandle(event));
		ASSERT_NE(eventType, 0u);
		using CompareHandles = BOOL (WINAPI*)(HANDLE, HANDLE);
		const HMODULE kernel = GetModuleHandleW(L"kernelbase.dll");
		const CompareHandles compareHandles = reinterpret_cast<CompareHandles>(GetProcAddress(kernel, "CompareObjectHandles"));
		ASSERT_NE(compareHandles, nullptr);
		for (core::uint32 index = 0; index < 32; ++index)
		{
			const std::vector<HandleEntry> iterationBefore = getHandles();
			ASSERT_FALSE(iterationBefore.empty());
			struct EventCandidate final
			{
				HANDLE Original;
				HANDLE Duplicate;
			};
			struct CloseCandidates final
			{
				std::vector<EventCandidate> Values;
				~CloseCandidates()
				{
					for (const EventCandidate& candidate : Values)
					{
						CloseHandle(candidate.Duplicate);
					}
				}
			} candidates;
			core::usize watcherEvent = 0;
			{
				core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(tracking, { rootText.data(), rootText.size() });
				ASSERT_TRUE(created);
				os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
				const std::vector<HandleEntry> active = getHandles();
				ASSERT_FALSE(active.empty());
				EXPECT_EQ(countFileHandles(active, directoryType, identity), baseline + 1);
				for (const HandleEntry& entry : active)
				{
					if (entry.Type != eventType || entry.Access != eventAccess ||
						hasHandle(iterationBefore, entry.Value, eventType, eventAccess))
					{
						continue;
					}
					EventInformation information{};
					if (!queryEvent(entry.Value, &information) || information.Type != 0 || information.State != 0)
					{
						continue;
					}
					HANDLE duplicate = nullptr;
					ASSERT_TRUE(DuplicateHandle(GetCurrentProcess(), entry.Value, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS));
					candidates.Values.push_back({ entry.Value, duplicate });
				}
				ASSERT_FALSE(candidates.Values.empty());
				const std::string name = "observed-" + std::to_string(index) + ".txt";
				{
					std::ofstream file(root / name);
					ASSERT_TRUE(file.good());
					file << "observed";
				}
				bool bIdentified = false;
				bool bObservedChange = false;
				const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
				while (!bIdentified && rox::core::MonotonicClock::now() < deadline)
				{
					std::vector<bool> signalled(candidates.Values.size());
					bool bAnySignalled = false;
					for (core::usize candidateIndex = 0; candidateIndex < candidates.Values.size(); ++candidateIndex)
					{
						EventInformation information{};
						ASSERT_TRUE(queryEvent(candidates.Values[candidateIndex].Duplicate, &information));
						signalled[candidateIndex] = information.State != 0;
						bAnySignalled |= signalled[candidateIndex];
					}
					if (!bAnySignalled)
					{
						rox::core::YieldThread();
						continue;
					}
					core::Result<os::FileChangeBatch> batch = watcher.Poll();
					ASSERT_TRUE(batch);
					ASSERT_FALSE(batch.Value().bRescanRequired);
					for (const os::FileChange& change : batch.Value().Changes)
					{
						bObservedChange |= std::string(change.RelativePath.Data(), change.RelativePath.Size()) == name;
					}
					core::usize resetCount = 0;
					for (core::usize candidateIndex = 0; candidateIndex < candidates.Values.size(); ++candidateIndex)
					{
						EventInformation information{};
						ASSERT_TRUE(queryEvent(candidates.Values[candidateIndex].Duplicate, &information));
						if (signalled[candidateIndex] && information.State == 0)
						{
							watcherEvent = candidateIndex;
							++resetCount;
						}
					}
					ASSERT_LE(resetCount, 1u) << "Multiple event candidates reset during one watch poll";
					bIdentified = bObservedChange && resetCount == 1;
					rox::core::YieldThread();
				}
				ASSERT_TRUE(bIdentified) << "Could not identify the event signalled and reset by the watcher";
				{
					std::ofstream file(root / ("pending-" + std::to_string(index) + ".txt"));
					ASSERT_TRUE(file.good());
					file << "pending";
				}
			}
			const std::vector<HandleEntry> after = getHandles();
			ASSERT_FALSE(after.empty());
			EXPECT_EQ(countFileHandles(after, directoryType, identity), baseline);
			const EventCandidate& eventHandle = candidates.Values[watcherEvent];
			EXPECT_FALSE(compareHandles(eventHandle.Original, eventHandle.Duplicate)) << "The watcher event handle remained open";
			EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
			EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
		}

	}
} // namespace
