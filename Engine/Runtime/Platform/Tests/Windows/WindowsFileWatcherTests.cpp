#include <WindowsFileWatcher.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>

#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	class WindowsFileWatcherTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			const rox::core::MonotonicClock::duration::rep nonce = rox::core::MonotonicClock::now().time_since_epoch().count();
			root = std::filesystem::temp_directory_path() / ("RoxellWatchTests-" + std::to_string(nonce));
			std::filesystem::create_directories(root);
			const std::u8string encoded = root.u8string();
			rootText.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
		}

		void TearDown() override
		{
			EXPECT_EQ(resource.GetStatistics().LiveAllocationCount, 0u);
			EXPECT_EQ(resource.GetStatistics().LiveByteCount, 0u);
			std::error_code error;
			std::filesystem::remove_all(root, error);
			EXPECT_FALSE(error);
		}

		core::StringView getPath() const
		{
			return { rootText.data(), rootText.size() };
		}

	protected:
		core::TrackingMemoryResource resource{ core::SystemMemoryResource::Get() };
		std::filesystem::path root;
		std::string rootText;
	};

	TEST_F(WindowsFileWatcherTests, DeliversCreatedFileAsRootRelativePath)
	{
		core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getPath());
		ASSERT_TRUE(created);
		os::WindowsFileWatcher watcher = core::Move(created).TakeValue();
		{
			std::ofstream output(root / "created.txt", std::ios::binary);
			ASSERT_TRUE(output.good());
			output << "change";
		}
		bool bFound = false;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!bFound && rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::FileChangeBatch> batch = watcher.Poll();
			ASSERT_TRUE(batch);
			ASSERT_FALSE(batch.Value().bRescanRequired);
			for (const os::FileChange& change : batch.Value().Changes)
			{
				if (change.Kind == os::EFileChangeKind::Created && change.RelativePath == "created.txt")
				{
					EXPECT_TRUE(change.OldRelativePath.IsEmpty());
					bFound = true;
				}
			}
			rox::core::YieldThread();
		}
		EXPECT_TRUE(bFound);
	}

	TEST_F(WindowsFileWatcherTests, InvalidOptionsReportErrorWithoutAllocating)
	{
		os::WindowsFileWatcherOptions options;
		options.MaximumChangesPerPoll = 0;
		const core::Result<os::WindowsFileWatcher> result = os::WindowsFileWatcher::Create(resource, getPath(), options);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-WATCH-OPTIONS");
	}

	TEST_F(WindowsFileWatcherTests, AllocationFailureReleasesPartialStorage)
	{
		for (core::usize attempts = 0; attempts < 2; ++attempts)
		{
			core::FailingMemoryResource failing(resource, attempts);
			const core::Result<os::WindowsFileWatcher> result = os::WindowsFileWatcher::Create(failing, getPath());
			ASSERT_FALSE(result);
			EXPECT_EQ(result.GetError().Code, core::EErrorCode::ExternalFailure);
			EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-WATCH-ALLOCATION");
			EXPECT_EQ(resource.GetStatistics().LiveAllocationCount, 0u);
			EXPECT_EQ(resource.GetStatistics().LiveByteCount, 0u);
		}
	}

	TEST_F(WindowsFileWatcherTests, MoveTransfersPendingWatchAndDestructionClosesIt)
	{
		{
			core::Result<os::WindowsFileWatcher> created = os::WindowsFileWatcher::Create(resource, getPath());
			ASSERT_TRUE(created);
			os::WindowsFileWatcher original = core::Move(created).TakeValue();
			os::WindowsFileWatcher moved(core::Move(original));
			EXPECT_FALSE(original.IsValid());
			EXPECT_TRUE(moved.IsValid());
		}
		std::error_code error;
		EXPECT_TRUE(std::filesystem::remove(root, error));
		EXPECT_FALSE(error);
	}
} // namespace
