#include <WindowsFileWatcher.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>

#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	struct ObservedChange
	{
		std::string Path;
		std::string OldPath;
		os::EFileChangeKind Kind;
	};

	std::string copyText(const core::StringView text)
	{
		return text.IsEmpty() ? std::string() : std::string(text.Data(), text.Size());
	}

	bool collectUntil(os::WindowsFileWatcher& watcher, const core::StringView path, const os::EFileChangeKind kind, std::vector<ObservedChange>* const changes, const bool bAllowRescan = false)
	{
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bFound = false;
		while (!bFound && rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::FileChangeBatch> batch = watcher.Poll();
			if (!batch)
			{
				ADD_FAILURE() << "Poll failed: " << batch.GetError().DiagnosticID.CStr();
				return false;
			}
			if (batch.Value().bRescanRequired && !bAllowRescan)
			{
				ADD_FAILURE() << "Unexpected incomplete change batch";
				return false;
			}
			for (const os::FileChange& change : batch.Value().Changes)
			{
				changes->push_back({ copyText(change.RelativePath), copyText(change.OldRelativePath), change.Kind });
				bFound |= change.RelativePath == path && change.Kind == kind;
			}
			rox::core::YieldThread();
		}
		return bFound;
	}

	class WindowsFileWatcherChangesTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			const rox::core::MonotonicClock::duration::rep nonce = rox::core::MonotonicClock::now().time_since_epoch().count();
			base = std::filesystem::temp_directory_path() / ("RoxellWatchChanges-" + std::to_string(nonce));
			root = base / "root";
			sibling = base / "sibling";
			std::filesystem::create_directories(root);
			std::filesystem::create_directories(sibling);
			const std::u8string encoded = root.u8string();
			rootText.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
		}

		void TearDown() override
		{
			EXPECT_EQ(resource.GetStatistics().LiveAllocationCount, 0u);
			EXPECT_EQ(resource.GetStatistics().LiveByteCount, 0u);
			std::error_code error;
			std::filesystem::remove_all(base, error);
			EXPECT_FALSE(error);
		}

		core::StringView getRootText() const
		{
			return { rootText.data(), rootText.size() };
		}

	protected:
		core::TrackingMemoryResource resource{ core::SystemMemoryResource::Get() };
		std::filesystem::path base;
		std::filesystem::path root;
		std::filesystem::path sibling;
		std::string rootText;
	};

	TEST_F(WindowsFileWatcherChangesTests, ReportsRecursiveChangesWithoutSiblingChanges)
	{
		std::filesystem::create_directory(root / "nested");
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText());
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		{
			std::ofstream outside(sibling / "outside.txt");
			std::ofstream inside(root / "nested" / "inside.txt");
			ASSERT_TRUE(outside.good());
			ASSERT_TRUE(inside.good());
			outside << "outside";
			inside << "inside";
		}
		std::vector<ObservedChange> changes;
		ASSERT_TRUE(collectUntil(watcher, "nested/inside.txt", os::EFileChangeKind::Created, &changes));
		for (const ObservedChange& change : changes)
		{
			EXPECT_EQ(change.Path.find("outside.txt"), std::string::npos);
		}
		{
			std::ofstream inside(root / "nested" / "inside.txt", std::ios::app);
			ASSERT_TRUE(inside.good());
			inside << " extended";
		}
		ASSERT_TRUE(collectUntil(watcher, "nested/inside.txt", os::EFileChangeKind::Modified, &changes));
		ASSERT_TRUE(std::filesystem::remove(root / "nested" / "inside.txt"));
		EXPECT_TRUE(collectUntil(watcher, "nested/inside.txt", os::EFileChangeKind::Deleted, &changes));
	}

	TEST_F(WindowsFileWatcherChangesTests, RenameWithinRootPairsSourceAndDestination)
	{
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText());
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		{
			std::ofstream file(root / "source.txt");
			ASSERT_TRUE(file.good());
			file << "content";
		}
		std::vector<ObservedChange> changes;
		ASSERT_TRUE(collectUntil(watcher, "source.txt", os::EFileChangeKind::Created, &changes));
		changes.clear();
		std::filesystem::rename(root / "source.txt", root / "target.txt");
		ASSERT_TRUE(collectUntil(watcher, "target.txt", os::EFileChangeKind::Renamed, &changes));
		for (const ObservedChange& change : changes)
		{
			if (change.Path == "target.txt" && change.Kind == os::EFileChangeKind::Renamed)
			{
				EXPECT_EQ(change.OldPath, "source.txt");
			}
			EXPECT_FALSE(change.Path == "source.txt" && change.Kind == os::EFileChangeKind::Deleted);
			EXPECT_FALSE(change.Path == "target.txt" && change.Kind == os::EFileChangeKind::Created);
		}
	}

	TEST_F(WindowsFileWatcherChangesTests, MovingAcrossRootReportsDeleteAndCreate)
	{
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText());
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		{
			std::ofstream file(root / "leaving.txt");
			ASSERT_TRUE(file.good());
			file << "content";
		}
		std::vector<ObservedChange> changes;
		ASSERT_TRUE(collectUntil(watcher, "leaving.txt", os::EFileChangeKind::Created, &changes));
		changes.clear();
		std::filesystem::rename(root / "leaving.txt", sibling / "left.txt");
		ASSERT_TRUE(collectUntil(watcher, "leaving.txt", os::EFileChangeKind::Deleted, &changes));
		for (const ObservedChange& change : changes)
		{
			EXPECT_FALSE(change.Path == "leaving.txt" && change.Kind == os::EFileChangeKind::Renamed);
		}
		changes.clear();
		{
			std::ofstream file(sibling / "incoming.txt");
			ASSERT_TRUE(file.good());
			file << "content";
		}
		std::filesystem::rename(sibling / "incoming.txt", root / "arrived.txt");
		ASSERT_TRUE(collectUntil(watcher, "arrived.txt", os::EFileChangeKind::Created, &changes));
		for (const ObservedChange& change : changes)
		{
			EXPECT_FALSE(change.Path == "arrived.txt" && change.Kind == os::EFileChangeKind::Renamed);
		}
	}
	TEST_F(WindowsFileWatcherChangesTests, BatchesAreSortedAndDeduplicated)
	{
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText());
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		std::set<std::string> expected;
		for (int index = 63; index >= 0; --index)
		{
			char name[32] = {};
			std::snprintf(name, sizeof(name), "order-%02d.txt", index);
			expected.insert(name);
			std::ofstream file(root / name);
			ASSERT_TRUE(file.good());
			file << "x";
		}
		std::set<std::string> arrived;
		core::usize adjacentPairs = 0;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (arrived != expected && rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::FileChangeBatch> batch = watcher.Poll();
			ASSERT_TRUE(batch);
			ASSERT_FALSE(batch.Value().bRescanRequired);
			std::string previousPath;
			os::EFileChangeKind previousKind = os::EFileChangeKind::Created;
			bool bHasPrevious = false;
			for (const os::FileChange& change : batch.Value().Changes)
			{
				const std::string path = copyText(change.RelativePath);
				if (bHasPrevious)
				{
					EXPECT_TRUE(previousPath < path || (previousPath == path && previousKind < change.Kind));
					++adjacentPairs;
				}
				previousPath = path;
				previousKind = change.Kind;
				bHasPrevious = true;
				arrived.insert(path);
			}
			rox::core::YieldThread();
		}
		EXPECT_EQ(arrived, expected);
		EXPECT_GT(adjacentPairs, 0u);
	}

	TEST_F(WindowsFileWatcherChangesTests, RepeatedModificationsCollapseWithinBatch)
	{
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText());
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		for (int index = 0; index < 128; ++index)
		{
			std::ofstream file(root / "churned.txt", std::ios::trunc);
			ASSERT_TRUE(file.good());
			file << index;
		}
		bool bModified = false;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!bModified && rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::FileChangeBatch> batch = watcher.Poll();
			ASSERT_TRUE(batch);
			ASSERT_FALSE(batch.Value().bRescanRequired);
			core::usize modifiedCount = 0;
			for (const os::FileChange& change : batch.Value().Changes)
			{
				if (change.RelativePath == "churned.txt" && change.Kind == os::EFileChangeKind::Modified)
				{
					++modifiedCount;
				}
			}
			EXPECT_LE(modifiedCount, 1u);
			bModified |= modifiedCount == 1;
			rox::core::YieldThread();
		}
		EXPECT_TRUE(bModified);
	}

	TEST_F(WindowsFileWatcherChangesTests, ChangeLimitRequestsRescanAndWatchContinues)
	{
		os::WindowsFileWatcherOptions options;
		options.MaximumChangesPerPoll = 4;
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText(), options);
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		for (int index = 0; index < 64; ++index)
		{
			std::ofstream file(root / ("cap-" + std::to_string(index) + ".txt"));
			ASSERT_TRUE(file.good());
			file << "x";
		}
		bool bRescan = false;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!bRescan && rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::FileChangeBatch> batch = watcher.Poll();
			ASSERT_TRUE(batch);
			EXPECT_LE(batch.Value().Changes.Size(), options.MaximumChangesPerPoll);
			bRescan |= batch.Value().bRescanRequired;
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bRescan);
		{
			std::ofstream file(root / "after-cap.txt");
			ASSERT_TRUE(file.good());
			file << "y";
		}
		std::vector<ObservedChange> changes;
		EXPECT_TRUE(collectUntil(watcher, "after-cap.txt", os::EFileChangeKind::Created, &changes, true));
	}

	TEST_F(WindowsFileWatcherChangesTests, NativeBufferOverflowRequestsRescanAndWatchContinues)
	{
		os::WindowsFileWatcherOptions options;
		options.NativeBufferBytes = 4 * 1024;
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText(), options);
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		for (int index = 0; index < 4096; ++index)
		{
			std::ofstream file(root / ("burst-" + std::to_string(index) + ".txt"));
			ASSERT_TRUE(file.good());
			file << "x";
		}
		bool bRescan = false;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!bRescan && rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::FileChangeBatch> batch = watcher.Poll();
			ASSERT_TRUE(batch);
			bRescan |= batch.Value().bRescanRequired;
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bRescan);
		{
			std::ofstream file(root / "after-overflow.txt");
			ASSERT_TRUE(file.good());
			file << "y";
		}
		std::vector<ObservedChange> changes;
		EXPECT_TRUE(collectUntil(watcher, "after-overflow.txt", os::EFileChangeKind::Created, &changes, true));
	}

	TEST_F(WindowsFileWatcherChangesTests, RemovedRootProducesPersistentNotFound)
	{
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText());
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		std::error_code error;
		std::filesystem::remove_all(root, error);
		ASSERT_FALSE(error);
		bool bGone = false;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!bGone && rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::FileChangeBatch> batch = watcher.Poll();
			if (!batch)
			{
				EXPECT_EQ(batch.GetError().Code, core::EErrorCode::NotFound);
				EXPECT_EQ(batch.GetError().DiagnosticID, "ROX-PLATFORM-WATCH-ROOT-GONE");
				bGone = true;
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bGone);
		const core::Result<os::FileChangeBatch> again = watcher.Poll();
		ASSERT_FALSE(again);
		EXPECT_EQ(again.GetError().Code, core::EErrorCode::NotFound);
		EXPECT_EQ(again.GetError().DiagnosticID, "ROX-PLATFORM-WATCH-ROOT-GONE");
	}

	TEST_F(WindowsFileWatcherChangesTests, RejectsOutOfRangeCapacitiesAndInvalidRoots)
	{
		os::WindowsFileWatcherOptions options;
		options.MaximumChangesPerPoll = os::MAXIMUM_FILE_CHANGE_COUNT + 1;
		core::Result<os::WindowsFileWatcher> tooMany = os::WindowsFileWatcher::Create(resource, getRootText(), options);
		ASSERT_FALSE(tooMany);
		EXPECT_EQ(tooMany.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(tooMany.GetError().DiagnosticID, "ROX-PLATFORM-WATCH-OPTIONS");
		options = {};
		options.NativeBufferBytes = os::MAXIMUM_FILE_WATCH_BUFFER_BYTES + 4;
		core::Result<os::WindowsFileWatcher> tooLarge = os::WindowsFileWatcher::Create(resource, getRootText(), options);
		ASSERT_FALSE(tooLarge);
		EXPECT_EQ(tooLarge.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(tooLarge.GetError().DiagnosticID, "ROX-PLATFORM-WATCH-OPTIONS");
		core::Result<os::WindowsFileWatcher> empty = os::WindowsFileWatcher::Create(resource, {});
		ASSERT_FALSE(empty);
		EXPECT_EQ(empty.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(empty.GetError().DiagnosticID, "ROX-PLATFORM-WATCH-ROOT");
		const std::u8string missingEncoded = (root / "missing").u8string();
		const std::string missingText(reinterpret_cast<const char*>(missingEncoded.data()), missingEncoded.size());
		core::Result<os::WindowsFileWatcher> missing = os::WindowsFileWatcher::Create(resource, { missingText.data(), missingText.size() });
		ASSERT_FALSE(missing);
		EXPECT_EQ(missing.GetError().Code, core::EErrorCode::NotFound);
		EXPECT_EQ(missing.GetError().DiagnosticID, "ROX-PLATFORM-WATCH-ROOT-GONE");
		{
			std::ofstream file(root / "file.txt");
			ASSERT_TRUE(file.good());
			file << "x";
		}
		const std::u8string fileEncoded = (root / "file.txt").u8string();
		const std::string fileText(reinterpret_cast<const char*>(fileEncoded.data()), fileEncoded.size());
		core::Result<os::WindowsFileWatcher> file = os::WindowsFileWatcher::Create(resource, { fileText.data(), fileText.size() });
		ASSERT_FALSE(file);
		EXPECT_EQ(file.GetError().DiagnosticID, "ROX-PLATFORM-WATCH-ARM");
	}

	TEST_F(WindowsFileWatcherChangesTests, RepeatedDestructionRetiresPendingChanges)
	{
		for (int index = 0; index < 32; ++index)
		{
			{
				core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText());
				ASSERT_TRUE(created);
				os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
				std::ofstream file(root / ("pending-" + std::to_string(index) + ".txt"));
				ASSERT_TRUE(file.good());
				file << "pending";
			}
			EXPECT_EQ(resource.GetStatistics().LiveAllocationCount, 0u);
			EXPECT_EQ(resource.GetStatistics().LiveByteCount, 0u);
		}
	}

	TEST_F(WindowsFileWatcherChangesTests, ForeignThreadPollTriggersOwnershipAssertion)
	{
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getRootText());
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		ASSERT_DEATH(
			{
				std::thread foreign([&watcher]() { (void)watcher.Poll(); });
				foreign.join();
			},
			"A file watcher must be polled on the thread that created it.*IsOwnerThread.*WindowsFileWatcher.cpp");
	}

} // namespace
