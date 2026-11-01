#include <WindowsFileSystem.h>
#include <Utility.hpp>
#include <Synchronization.hpp>
#include <ClockPrimitives.hpp>
#include "../../Private/Windows/WindowsPath.h"
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>

using namespace rox::core;
using namespace rox::os;

namespace
{
	std::string utf8(const std::filesystem::path& path)
	{
		const std::u8string bytes = path.u8string();
		return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
	}

	template <size_t N>
	Span<const Byte> bytes(const char (&text)[N])
	{
		return { reinterpret_cast<const Byte*>(text), N - 1 };
	}

	std::string text(const Array<Byte>& value)
	{
		return { reinterpret_cast<const char*>(value.Data()), value.Size() };
	}

	class WindowsFileSystemTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			static rox::core::Atomic<unsigned long> sequence{ 0 };
			m_Root = std::filesystem::temp_directory_path() /
				("RoxellFileSystem-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(sequence++));
			m_RootText = utf8(m_Root);
			Result<FileSystemOwner> created = CreateWindowsFileSystem(SystemMemoryResource::Get(), m_RootText);
			ASSERT_TRUE(created);
			m_Files = rox::core::Move(created).TakeValue();
		}

		void TearDown() override
		{
			m_Files.Reset();
			std::error_code error;
			std::filesystem::remove_all(m_Root, error);
			EXPECT_FALSE(error) << error.message();
		}

	protected:
		std::filesystem::path m_Root;
		std::string m_RootText;
		FileSystemOwner m_Files;
	};
}

TEST_F(WindowsFileSystemTests, CreateNewAndReplacePublishExpectedUtf8PathBytes)
{
	const StringView path = "nested/한글.bin";
	Result<EAtomicWriteOutcome> first = m_Files->WriteFileAtomically(path, bytes("first"), EAtomicWritePolicy::CreateNew);
	ASSERT_TRUE(first);
	EXPECT_EQ(first.Value(), EAtomicWriteOutcome::Published);
	Result<EAtomicWriteOutcome> duplicate = m_Files->WriteFileAtomically(path, bytes("other"), EAtomicWritePolicy::CreateNew);
	ASSERT_TRUE(duplicate);
	EXPECT_EQ(duplicate.Value(), EAtomicWriteOutcome::AlreadyExists);
	Result<Array<Byte>> before = m_Files->ReadFile(path, 5);
	ASSERT_TRUE(before);
	EXPECT_EQ(text(before.Value()), "first");
	Result<EAtomicWriteOutcome> replacement = m_Files->WriteFileAtomically(path, bytes("replacement"), EAtomicWritePolicy::ReplaceExisting);
	ASSERT_TRUE(replacement);
	EXPECT_EQ(replacement.Value(), EAtomicWriteOutcome::Published);
	Result<Array<Byte>> after = m_Files->ReadFile(path, 11);
	ASSERT_TRUE(after);
	EXPECT_EQ(text(after.Value()), "replacement");
}

TEST_F(WindowsFileSystemTests, ReadLimitsAndAllocationFailurePreserveFile)
{
	ASSERT_TRUE(m_Files->WriteFileAtomically("data", bytes("hello"), EAtomicWritePolicy::CreateNew));
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracking, 0);
	Result<Array<Byte>> limited = m_Files->ReadFile("data", 4, failing);
	ASSERT_FALSE(limited);
	EXPECT_EQ(limited.GetError().Code, EErrorCode::Unsupported);
	EXPECT_EQ(limited.GetError().DiagnosticID, "ROX-PLATFORM-FILE-LIMIT");
	EXPECT_EQ(failing.GetAllocationAttemptCount(), 0);
	Result<Array<Byte>> zero = m_Files->ReadFile("data", 0, failing);
	ASSERT_FALSE(zero);
	EXPECT_EQ(zero.GetError().Code, EErrorCode::InvalidArgument);
	EXPECT_EQ(zero.GetError().DiagnosticID, "ROX-PLATFORM-FILE-LIMIT-ZERO");
	Result<Array<Byte>> failed = m_Files->ReadFile("data", 5, failing);
	ASSERT_FALSE(failed);
	EXPECT_EQ(failed.GetError().Code, EErrorCode::ExternalFailure);
	EXPECT_EQ(failed.GetError().DiagnosticID, "ROX-PLATFORM-FILE-ALLOCATION");
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	{
		Result<Array<Byte>> read = m_Files->ReadFile("data", 5, tracking);
		ASSERT_TRUE(read);
		EXPECT_EQ(text(read.Value()), "hello");
		EXPECT_EQ(&read.Value().GetResource(), &tracking);
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0);
}

TEST_F(WindowsFileSystemTests, InvalidAndEscapingPathsAreRejected)
{
	for (const StringView path : { StringView("../outside"), StringView("C:/outside"), StringView("NUL"), StringView("cache/nul.txt"), StringView("COM1/inner"), StringView("COM\xc2\xb9") })
	{
		SCOPED_TRACE(std::string(path.Data(), path.Size()));
		Result<bool> result = m_Files->Exists(path);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, EErrorCode::PermissionDenied);
	}
	Result<bool> ordinary = m_Files->Exists("console.bin");
	ASSERT_TRUE(ordinary);
	EXPECT_FALSE(ordinary.Value());
	EXPECT_FALSE(OpenWindowsFileSystem(SystemMemoryResource::Get(), ""));
	EXPECT_FALSE(OpenWindowsFileSystem(SystemMemoryResource::Get(), StringView("x\0y", 3)));
	EXPECT_FALSE(OpenWindowsFileSystem(SystemMemoryResource::Get(), StringView("\xff", 1)));
}

TEST_F(WindowsFileSystemTests, BlockedReplacementPreservesOriginalAndCleansTemporaryFile)
{
	ASSERT_TRUE(m_Files->WriteFileAtomically("blocked", bytes("before"), EAtomicWritePolicy::CreateNew));
	const HANDLE blocker = CreateFileW((m_Root / "blocked").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(blocker, INVALID_HANDLE_VALUE);
	const rox::core::MonotonicClock::time_point started = rox::core::MonotonicClock::now();
	Result<EAtomicWriteOutcome> failed = m_Files->WriteFileAtomically("blocked", bytes("after"), EAtomicWritePolicy::ReplaceExisting);
	const rox::core::MonotonicClock::duration elapsed = rox::core::MonotonicClock::now() - started;
	EXPECT_TRUE(CloseHandle(blocker));
	EXPECT_LT(elapsed, rox::core::Seconds(2));
	ASSERT_FALSE(failed);
	EXPECT_EQ(failed.GetError().DiagnosticID, "ROX-PLATFORM-FILESYSTEM");
	Result<Array<Byte>> preserved = m_Files->ReadFile("blocked", 64);
	ASSERT_TRUE(preserved);
	EXPECT_EQ(text(preserved.Value()), "before");
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(m_Root))
	{
		EXPECT_EQ(entry.path().filename().native().find(L".roxell-tmp-"), std::wstring::npos);
	}
	ASSERT_TRUE(m_Files->WriteFileAtomically("blocked", bytes("after"), EAtomicWritePolicy::ReplaceExisting));
	Result<Array<Byte>> published = m_Files->ReadFile("blocked", 64);
	ASSERT_TRUE(published);
	EXPECT_EQ(text(published.Value()), "after");
}

TEST_F(WindowsFileSystemTests, RemoveIsIdempotentAndRefusesDirectories)
{
	ASSERT_TRUE(m_Files->WriteFileAtomically("dir/file", bytes("keep"), EAtomicWritePolicy::CreateNew));
	Result<void> directory = m_Files->RemoveFile("dir");
	ASSERT_FALSE(directory);
	EXPECT_EQ(directory.GetError().Code, EErrorCode::PermissionDenied);
	Result<Array<Byte>> preserved = m_Files->ReadFile("dir/file", 4);
	ASSERT_TRUE(preserved);
	EXPECT_EQ(text(preserved.Value()), "keep");
	ASSERT_TRUE(m_Files->RemoveFile("dir/file"));
	ASSERT_TRUE(m_Files->RemoveFile("dir/file"));
	Result<bool> exists = m_Files->Exists("dir/file");
	ASSERT_TRUE(exists);
	EXPECT_FALSE(exists.Value());
}

TEST_F(WindowsFileSystemTests, LockConflictsAcrossAliasesAndMoveResetAllowsReacquisition)
{
	Result<FileSystemOwner> aliasResult = OpenWindowsFileSystem(SystemMemoryResource::Get(), utf8(m_Root / "."));
	ASSERT_TRUE(aliasResult);
	FileSystemOwner alias = rox::core::Move(aliasResult).TakeValue();
	Result<FileSystemOwner> sameRootResult = OpenWindowsFileSystem(SystemMemoryResource::Get(), m_RootText);
	ASSERT_TRUE(sameRootResult);
	FileSystemOwner sameRoot = rox::core::Move(sameRootResult).TakeValue();
	Result<ExclusiveFileLock> acquired = m_Files->TryAcquireExclusiveFileLock("writer.lock");
	ASSERT_TRUE(acquired);
	ExclusiveFileLock original = rox::core::Move(acquired).TakeValue();
	ExclusiveFileLock moved = rox::core::Move(original);
	EXPECT_FALSE(original.IsValid());
	EXPECT_TRUE(moved.IsValid());
	Result<ExclusiveFileLock> sameRootConflict = sameRoot->TryAcquireExclusiveFileLock("writer.lock");
	ASSERT_FALSE(sameRootConflict);
	EXPECT_EQ(sameRootConflict.GetError().Code, EErrorCode::Conflict);
	EXPECT_EQ(sameRootConflict.GetError().DiagnosticID, "ROX-PLATFORM-FILE-LOCK-CONFLICT");
	Result<ExclusiveFileLock> conflict = alias->TryAcquireExclusiveFileLock("writer.lock");
	ASSERT_FALSE(conflict);
	EXPECT_EQ(conflict.GetError().Code, EErrorCode::Conflict);
	EXPECT_EQ(conflict.GetError().DiagnosticID, "ROX-PLATFORM-FILE-LOCK-CONFLICT");
	moved.Reset();
	EXPECT_FALSE(moved.IsValid());
	Result<bool> exists = m_Files->Exists("writer.lock");
	ASSERT_TRUE(exists);
	EXPECT_FALSE(exists.Value());
	Result<ExclusiveFileLock> reacquired = alias->TryAcquireExclusiveFileLock("writer.lock");
	EXPECT_TRUE(reacquired);
}

TEST_F(WindowsFileSystemTests, OwnerAllocationFailureDoesNotCreateRootOrLeak)
{
	const std::string missing = utf8(m_Root / "missing");
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracking, 0);
	Result<FileSystemOwner> result = CreateWindowsFileSystem(failing, missing);
	ASSERT_FALSE(result);
	EXPECT_EQ(result.GetError().Code, EErrorCode::ExternalFailure);
	EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-FILESYSTEM-ALLOCATION");
	EXPECT_EQ(failing.GetAllocationAttemptCount(), 1);
	EXPECT_FALSE(std::filesystem::exists(m_Root / "missing"));
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
}

TEST_F(WindowsFileSystemTests, DirectoryInventoryIsSortedBoundedAndCancellable)
{
	std::filesystem::create_directories(m_Root / "nested/deep");
	std::ofstream(m_Root / "a.bin") << "a";
	std::ofstream(m_Root / "nested/b.bin") << "b";
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		Result<Array<String>> files = ReadWindowsDirectoryFiles(m_RootText, tracking);
		ASSERT_TRUE(files);
		ASSERT_EQ(files.Value().Size(), 2);
		EXPECT_EQ(files.Value()[0].View(), "a.bin");
		EXPECT_EQ(files.Value()[1].View(), "nested/b.bin");
		Result<Array<String>> entriesLimited = ReadWindowsDirectoryFiles(m_RootText, tracking, 1);
		ASSERT_FALSE(entriesLimited);
		EXPECT_EQ(entriesLimited.GetError().Code, EErrorCode::Conflict);
		EXPECT_EQ(entriesLimited.GetError().DiagnosticID, "ROX-PLATFORM-DIRECTORY-INVENTORY");
		Result<Array<String>> depthLimited = ReadWindowsDirectoryFiles(m_RootText, tracking, 32, 1);
		ASSERT_FALSE(depthLimited);
		EXPECT_EQ(depthLimited.GetError().Code, EErrorCode::Conflict);
		EXPECT_EQ(depthLimited.GetError().DiagnosticID, "ROX-PLATFORM-DIRECTORY-INVENTORY");
		Result<Array<String>> invalid = ReadWindowsDirectoryFiles(m_RootText, tracking, 0);
		ASSERT_FALSE(invalid);
		EXPECT_EQ(invalid.GetError().Code, EErrorCode::InvalidArgument);
		EXPECT_EQ(invalid.GetError().DiagnosticID, "ROX-PLATFORM-DIRECTORY-INVENTORY");
		CancellationSource cancellation = CancellationSource::Create(tracking);
		ASSERT_TRUE(cancellation.IsValid());
		cancellation.RequestCancellation();
		Result<Array<String>> cancelled = ReadWindowsDirectoryFiles(m_RootText, tracking, 32, 32, cancellation.GetToken());
		ASSERT_FALSE(cancelled);
		EXPECT_EQ(cancelled.GetError().Code, EErrorCode::Cancelled);
		EXPECT_EQ(cancelled.GetError().DiagnosticID, "ROX-PLATFORM-DIRECTORY-INVENTORY");
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0);
}

TEST_F(WindowsFileSystemTests, DirectoryInventoryAllocationFailuresReleaseAllMemory)
{
	std::filesystem::create_directories(m_Root / "nested");
	std::ofstream(m_Root / "nested/file") << "data";
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	bool bReachedSuccess = false;
	for (usize allowed = 0; allowed < 128; ++allowed)
	{
		FailingMemoryResource failing(tracking, allowed);
		{
			Result<Array<String>> result = ReadWindowsDirectoryFiles(m_RootText, failing);
			if (result)
			{
				ASSERT_EQ(result.Value().Size(), 1);
				EXPECT_EQ(result.Value()[0].View(), "nested/file");
				bReachedSuccess = true;
			}
			else
			{
				EXPECT_EQ(result.GetError().Code, EErrorCode::ExternalFailure);
				EXPECT_GT(failing.GetAllocationAttemptCount(), allowed);
			}
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0);
		if (bReachedSuccess)
		{
			break;
		}
	}
	EXPECT_TRUE(bReachedSuccess);
}

TEST_F(WindowsFileSystemTests, DirectoryInventoryRejectsSymbolicLinks)
{
	std::filesystem::create_directories(m_Root / "target");
	const std::filesystem::path link = m_Root / "link";
	if (!CreateSymbolicLinkW(link.c_str(), (m_Root / "target").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
	{
		GTEST_SKIP() << "Creating symbolic links is unavailable: " << GetLastError();
	}
	Result<Array<String>> descendantLink = ReadWindowsDirectoryFiles(m_RootText, SystemMemoryResource::Get());
	ASSERT_FALSE(descendantLink);
	EXPECT_EQ(descendantLink.GetError().Code, EErrorCode::Conflict);
	EXPECT_EQ(descendantLink.GetError().DiagnosticID, "ROX-PLATFORM-DIRECTORY-INVENTORY");
	Result<Array<String>> rootLink = ReadWindowsDirectoryFiles(utf8(link), SystemMemoryResource::Get());
	ASSERT_FALSE(rootLink);
	EXPECT_EQ(rootLink.GetError().Code, EErrorCode::Conflict);
	EXPECT_EQ(rootLink.GetError().DiagnosticID, "ROX-PLATFORM-DIRECTORY-INVENTORY");
}

TEST_F(WindowsFileSystemTests, StandaloneFileOperationsPublishReadAndReportWriteStamp)
{
	const std::string path = utf8(m_Root / "standalone.bin");
	MemoryResource& memory = SystemMemoryResource::Get();
	ASSERT_TRUE(WriteWindowsFileAtomically(path, bytes("standalone"), memory));
	Result<Array<Byte>> read = ReadWindowsFile(path, 10, memory);
	ASSERT_TRUE(read);
	EXPECT_EQ(text(read.Value()), "standalone");
	Result<uint64> stamp = GetWindowsFileWriteStamp(path, memory);
	ASSERT_TRUE(stamp);
	EXPECT_NE(stamp.Value(), 0);
	Result<Array<Byte>> limited = ReadWindowsFile(path, 9, memory);
	ASSERT_FALSE(limited);
	EXPECT_EQ(limited.GetError().Code, EErrorCode::Unsupported);
	Result<Array<Byte>> preserved = ReadWindowsFile(path, 10, memory);
	ASSERT_TRUE(preserved);
	EXPECT_EQ(text(preserved.Value()), "standalone");
}

TEST_F(WindowsFileSystemTests, ContainmentUsesPathComponentsAndAllowsMissingDescendants)
{
	Result<bool> equal = IsWindowsPathWithinRoot(m_RootText, m_RootText);
	ASSERT_TRUE(equal);
	EXPECT_TRUE(equal.Value());
	Result<bool> descendant = IsWindowsPathWithinRoot(m_RootText, utf8(m_Root / "missing/child"));
	ASSERT_TRUE(descendant);
	EXPECT_TRUE(descendant.Value());
	Result<bool> sibling = IsWindowsPathWithinRoot(m_RootText, m_RootText + "-other/child");
	ASSERT_TRUE(sibling);
	EXPECT_FALSE(sibling.Value());
	EXPECT_FALSE(IsWindowsPathWithinRoot("", m_RootText));
}

TEST_F(WindowsFileSystemTests, OpenRequiresExistingDirectoryAndOwnerReleasesItsResource)
{
	MemoryResource& memory = SystemMemoryResource::Get();
	Result<FileSystemOwner> missing = OpenWindowsFileSystem(memory, utf8(m_Root / "missing"));
	ASSERT_FALSE(missing);
	EXPECT_EQ(missing.GetError().Code, EErrorCode::NotFound);
	EXPECT_FALSE(std::filesystem::exists(m_Root / "missing"));
	TrackingMemoryResource tracking(memory);
	{
		Result<FileSystemOwner> opened = OpenWindowsFileSystem(tracking, m_RootText);
		ASSERT_TRUE(opened);
		FileSystemOwner owner = rox::core::Move(opened).TakeValue();
		EXPECT_EQ(owner.GetResource(), &tracking);
		FileSystemOwner moved = rox::core::Move(owner);
		EXPECT_FALSE(owner);
		EXPECT_TRUE(moved);
		EXPECT_EQ(moved.GetResource(), &tracking);
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0);
}

TEST_F(WindowsFileSystemTests, LockConflictsWithAnotherProcessAndIsReleasedOnReset)
{
	wchar_t inheritedRoot[32768]{};
	const DWORD inheritedLength = GetEnvironmentVariableW(L"ROXELL_TEST_LOCK_ROOT", inheritedRoot, 32768);
	if (inheritedLength > 1 && inheritedLength < 32768)
	{
		Result<FileSystemOwner> opened = OpenWindowsFileSystem(SystemMemoryResource::Get(), utf8(inheritedRoot + 1));
		ASSERT_TRUE(opened);
		FileSystemOwner files = rox::core::Move(opened).TakeValue();
		Result<ExclusiveFileLock> acquired = files->TryAcquireExclusiveFileLock("process.lock");
		if (inheritedRoot[0] == L'+')
		{
			ASSERT_TRUE(acquired);
			ExclusiveFileLock lock = rox::core::Move(acquired).TakeValue();
			EXPECT_TRUE(lock.IsValid());
			lock.Reset();
		}
		else
		{
			ASSERT_FALSE(acquired);
			EXPECT_EQ(acquired.GetError().Code, EErrorCode::Conflict);
			EXPECT_EQ(acquired.GetError().DiagnosticID, "ROX-PLATFORM-FILE-LOCK-CONFLICT");
		}
		return;
	}

	Result<ExclusiveFileLock> acquired = m_Files->TryAcquireExclusiveFileLock("process.lock");
	ASSERT_TRUE(acquired);
	ExclusiveFileLock lock = rox::core::Move(acquired).TakeValue();
	wchar_t executable[32768]{};
	ASSERT_GT(GetModuleFileNameW(nullptr, executable, 32768), 0);
	const auto runChild = [&](const bool bExpectSuccess)
	{
		std::wstring command = L"\"" + std::wstring(executable) + L"\" --gtest_filter=WindowsFileSystemTests.LockConflictsWithAnotherProcessAndIsReleasedOnReset";
		const std::wstring childRoot = (bExpectSuccess ? L"+" : L"-") + m_Root.native();
		ASSERT_TRUE(SetEnvironmentVariableW(L"ROXELL_TEST_LOCK_ROOT", childRoot.c_str()));
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const BOOL started = CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
		EXPECT_TRUE(SetEnvironmentVariableW(L"ROXELL_TEST_LOCK_ROOT", nullptr));
		ASSERT_TRUE(started);
		const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
		if (wait != WAIT_OBJECT_0)
		{
			TerminateProcess(process.hProcess, 1);
			WaitForSingleObject(process.hProcess, 10000);
		}
		DWORD exitCode = 1;
		EXPECT_TRUE(GetExitCodeProcess(process.hProcess, &exitCode));
		EXPECT_TRUE(CloseHandle(process.hThread));
		EXPECT_TRUE(CloseHandle(process.hProcess));
		EXPECT_EQ(wait, WAIT_OBJECT_0);
		EXPECT_EQ(exitCode, 0);
	};

	runChild(false);
	ASSERT_FALSE(HasFatalFailure());
	lock.Reset();
	runChild(true);
	ASSERT_FALSE(HasFatalFailure());
	Result<bool> exists = m_Files->Exists("process.lock");
	ASSERT_TRUE(exists);
	EXPECT_FALSE(exists.Value());
	Result<ExclusiveFileLock> reacquired = m_Files->TryAcquireExclusiveFileLock("process.lock");
	EXPECT_TRUE(reacquired);
}

TEST_F(WindowsFileSystemTests, SuperscriptDeviceNamesAreRejectedWithoutAllocatingReadBuffer)
{
	const StringView reserved[] = { "COM\xc2\xb9", "COM\xc2\xb2", "COM\xc2\xb3", "LPT\xc2\xb9", "LPT\xc2\xb2",
		"LPT\xc2\xb3", "cache/cOm\xc2\xb9.txt", "cache/lPt\xc2\xb2  .bin", "cache/COM\xc2\xb3/inner.bin", "cache/LPT\xc2\xb9  " };
	FailingMemoryResource failing(SystemMemoryResource::Get(), 0);
	for (const StringView path : reserved)
	{
		SCOPED_TRACE(std::string(path.Data(), path.Size()));
		Result<bool> exists = m_Files->Exists(path);
		ASSERT_FALSE(exists);
		EXPECT_EQ(exists.GetError().Code, EErrorCode::PermissionDenied);
		EXPECT_EQ(exists.GetError().DiagnosticID, "ROX-PLATFORM-PATH-DEVICE");
		EXPECT_EQ(exists.GetError().Message, "The path names a reserved Windows device rather than a file under the root.");
		EXPECT_EQ(exists.GetError().RecoveryHint, "Rename the path segment so it is not a reserved device name.");
		Result<Array<Byte>> read = m_Files->ReadFile(path, 64, failing);
		ASSERT_FALSE(read);
		EXPECT_EQ(read.GetError().DiagnosticID, "ROX-PLATFORM-PATH-DEVICE");
		Result<EAtomicWriteOutcome> write = m_Files->WriteFileAtomically(path, bytes("data"), EAtomicWritePolicy::ReplaceExisting);
		ASSERT_FALSE(write);
		EXPECT_EQ(write.GetError().DiagnosticID, "ROX-PLATFORM-PATH-DEVICE");
		Result<void> remove = m_Files->RemoveFile(path);
		ASSERT_FALSE(remove);
		EXPECT_EQ(remove.GetError().DiagnosticID, "ROX-PLATFORM-PATH-DEVICE");
		Result<ExclusiveFileLock> lock = m_Files->TryAcquireExclusiveFileLock(path);
		ASSERT_FALSE(lock);
		EXPECT_EQ(lock.GetError().DiagnosticID, "ROX-PLATFORM-PATH-DEVICE");
	}
	EXPECT_EQ(failing.GetAllocationAttemptCount(), 0);
	for (const StringView path : { StringView("cache/COM\xc2\xb9-tail.bin"), StringView("cache/LPT\xc2\xb2-tail.bin"), StringView("cache/report\xc2\xb3.bin") })
	{
		ASSERT_TRUE(m_Files->WriteFileAtomically(path, bytes("ordinary"), EAtomicWritePolicy::CreateNew));
		Result<Array<Byte>> ordinary = m_Files->ReadFile(path, 8);
		ASSERT_TRUE(ordinary);
		EXPECT_EQ(text(ordinary.Value()), "ordinary");
	}
}

TEST_F(WindowsFileSystemTests, NativePathConversionParentAndStampAllocationFailuresDoNotLeak)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		FailingMemoryResource failing(tracking, 0);
		Result<WindowsPath> path = PathFromUtf8(m_RootText, failing);
		ASSERT_FALSE(path);
		EXPECT_EQ(path.GetError().Code, EErrorCode::ExternalFailure);
		Result<uint64> stamp = GetWindowsFileWriteStamp(m_RootText, failing);
		ASSERT_FALSE(stamp);
		EXPECT_EQ(stamp.GetError().DiagnosticID, "ROX-PLATFORM-PATH-ALLOCATION");
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	{
		FailingMemoryResource failing(tracking, 128);
		Result<WindowsPath> input = PathFromUtf8(m_RootText, failing);
		ASSERT_TRUE(input);
		failing.Reset(0);
		Result<WindowsPath> parent = input.Value().Parent();
		ASSERT_FALSE(parent);
		EXPECT_EQ(parent.GetError().Code, EErrorCode::ExternalFailure);
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0);
}

TEST_F(WindowsFileSystemTests, CanonicalizationAllocationFailuresReleaseAllMemory)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	const std::string missing = m_RootText + "/missing/../missing/child";
	bool bReachedSuccess = false;
	for (usize allowed = 0; allowed < 128; ++allowed)
	{
		FailingMemoryResource failing(tracking, 128);
		{
			Result<WindowsPath> input = PathFromUtf8(missing, failing);
			ASSERT_TRUE(input);
			failing.Reset(allowed);
			SystemErrorCode error;
			Result<WindowsPath> absolute = AbsolutePath(input.Value(), error);
			if (absolute && !error)
			{
				Result<WindowsPath> canonical = WeaklyCanonicalPath(absolute.Value(), error);
				if (canonical && !error)
				{
					bReachedSuccess = true;
				}
				else
				{
					ASSERT_FALSE(canonical);
					EXPECT_EQ(canonical.GetError().Code, EErrorCode::ExternalFailure);
				}
			}
			else
			{
				ASSERT_FALSE(absolute);
				EXPECT_EQ(absolute.GetError().Code, EErrorCode::ExternalFailure);
			}
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
		if (bReachedSuccess)
		{
			break;
		}
	}
	EXPECT_TRUE(bReachedSuccess);
}

TEST_F(WindowsFileSystemTests, DirectoryCreationAllocationFailuresReleaseAllMemory)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	const std::string path = m_RootText + "/allocation-test/child";
	bool bReachedSuccess = false;
	for (usize allowed = 0; allowed < 128; ++allowed)
	{
		FailingMemoryResource failing(tracking, 128);
		{
			Result<WindowsPath> input = PathFromUtf8(path, failing);
			ASSERT_TRUE(input);
			SystemErrorCode error;
			Result<WindowsPath> absolute = AbsolutePath(input.Value(), error);
			ASSERT_TRUE(absolute);
			ASSERT_FALSE(error);
			failing.Reset(allowed);
			Result<void> created = CreatePathDirectories(absolute.Value(), error);
			if (created)
			{
				EXPECT_FALSE(error);
				bReachedSuccess = true;
			}
			else
			{
				EXPECT_EQ(created.GetError().Code, EErrorCode::ExternalFailure);
			}
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
		if (bReachedSuccess)
		{
			break;
		}
	}
	EXPECT_TRUE(bReachedSuccess);
	EXPECT_TRUE(std::filesystem::is_directory(m_Root / "allocation-test/child"));
}

TEST_F(WindowsFileSystemTests, LinkEscapingRootIsRejectedWithoutChangingTarget)
{
	const std::filesystem::path outside = m_Root.parent_path() / (m_Root.filename().native() + L"-outside");
	std::filesystem::create_directories(outside);
	std::ofstream(outside / "sentinel") << "preserved";
	const std::filesystem::path link = m_Root / "escape";
	if (!CreateSymbolicLinkW(link.c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
	{
		const DWORD nativeError = GetLastError();
		std::error_code error;
		std::filesystem::remove_all(outside, error);
		GTEST_SKIP() << "Creating symbolic links is unavailable: " << nativeError;
	}
	Result<Array<Byte>> read = m_Files->ReadFile("escape/sentinel", 64);
	EXPECT_FALSE(read);
	if (!read)
	{
		EXPECT_EQ(read.GetError().Code, EErrorCode::PermissionDenied);
		EXPECT_EQ(read.GetError().DiagnosticID, "ROX-PLATFORM-PATH-SYMLINK-ESCAPE");
	}
	Result<EAtomicWriteOutcome> write = m_Files->WriteFileAtomically("escape/sentinel", bytes("changed"), EAtomicWritePolicy::ReplaceExisting);
	EXPECT_FALSE(write);
	std::ifstream input(outside / "sentinel");
	std::string contents;
	input >> contents;
	EXPECT_EQ(contents, "preserved");
	input.close();
	std::error_code error;
	std::filesystem::remove_all(outside, error);
	EXPECT_FALSE(error);
}

TEST_F(WindowsFileSystemTests, MissingReadsReportNotFoundWithoutAllocating)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracking, 0);
	Result<Array<Byte>> defaultRead = m_Files->ReadFile("absent.bin", 64);
	Result<Array<Byte>> ownedRead = m_Files->ReadFile("absent.bin", 64, failing);
	ASSERT_FALSE(defaultRead);
	ASSERT_FALSE(ownedRead);
	for (const Error* error : { &defaultRead.GetError(), &ownedRead.GetError() })
	{
		EXPECT_EQ(error->Code, EErrorCode::NotFound);
		EXPECT_EQ(error->DiagnosticID, "ROX-PLATFORM-FILESYSTEM");
		EXPECT_EQ(error->Message, "Reading file size failed with operating-system error code 2.");
		EXPECT_EQ(error->RecoveryHint, "Check the root-relative path and filesystem permissions.");
	}
	EXPECT_EQ(failing.GetAllocationAttemptCount(), 0);
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	EXPECT_FALSE(std::filesystem::exists(m_Root / "absent.bin"));
}

TEST_F(WindowsFileSystemTests, RemoveRefusesRootAndEmptyDirectoryWithoutChangingThem)
{
	Result<void> rootRemoval = m_Files->RemoveFile(".");
	ASSERT_FALSE(rootRemoval);
	EXPECT_EQ(rootRemoval.GetError().Code, EErrorCode::PermissionDenied);
	EXPECT_EQ(rootRemoval.GetError().DiagnosticID, "ROX-PLATFORM-FILESYSTEM");
	EXPECT_EQ(rootRemoval.GetError().Message, "Removing the file failed with operating-system error code 5.");
	EXPECT_EQ(rootRemoval.GetError().RecoveryHint, "Check the root-relative path and filesystem permissions.");
	EXPECT_TRUE(std::filesystem::is_directory(m_Root));
	ASSERT_TRUE(std::filesystem::create_directory(m_Root / "empty"));
	Result<void> emptyRemoval = m_Files->RemoveFile("empty/.");
	ASSERT_FALSE(emptyRemoval);
	EXPECT_EQ(emptyRemoval.GetError().Code, EErrorCode::PermissionDenied);
	EXPECT_EQ(emptyRemoval.GetError().DiagnosticID, "ROX-PLATFORM-FILESYSTEM");
	EXPECT_TRUE(std::filesystem::is_directory(m_Root / "empty"));
	EXPECT_TRUE(m_Files->RemoveFile("missing-parent/file.bin"));
}

TEST_F(WindowsFileSystemTests, OpenRejectsFileRootWithoutChangingItsContents)
{
	ASSERT_TRUE(m_Files->WriteFileAtomically("root-file", bytes("input-root-file"), EAtomicWritePolicy::CreateNew));
	Result<FileSystemOwner> opened = OpenWindowsFileSystem(SystemMemoryResource::Get(), utf8(m_Root / "root-file"));
	ASSERT_FALSE(opened);
	EXPECT_EQ(opened.GetError().Code, EErrorCode::InvalidArgument);
	Result<Array<Byte>> preserved = m_Files->ReadFile("root-file", 64);
	ASSERT_TRUE(preserved);
	EXPECT_EQ(text(preserved.Value()), "input-root-file");
}

TEST_F(WindowsFileSystemTests, OpenAllocationFailureDoesNotChangeExistingRootOrLeak)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracking, 0);
	Result<FileSystemOwner> opened = OpenWindowsFileSystem(failing, m_RootText);
	ASSERT_FALSE(opened);
	EXPECT_EQ(opened.GetError().Code, EErrorCode::ExternalFailure);
	EXPECT_EQ(opened.GetError().DiagnosticID, "ROX-PLATFORM-FILESYSTEM-ALLOCATION");
	EXPECT_EQ(failing.GetAllocationAttemptCount(), 1);
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0);
	EXPECT_TRUE(std::filesystem::is_directory(m_Root));
}
