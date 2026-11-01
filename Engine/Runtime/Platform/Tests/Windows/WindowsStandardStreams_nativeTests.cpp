#include <WindowsStandardStreams.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>

#include <string>
#include <vector>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	DWORD currentThreadCount()
	{
		const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
		{
			return 0;
		}
		THREADENTRY32 entry{};
		entry.dwSize = sizeof(entry);
		DWORD count = 0;
		if (Thread32First(snapshot, &entry))
		{
			do
			{
				if (entry.th32OwnerProcessID == GetCurrentProcessId())
				{
					++count;
				}
			} while (Thread32Next(snapshot, &entry));
		}
		CloseHandle(snapshot);
		return count;
	}

	bool stableProcessCounts(DWORD* const outHandles, DWORD* const outThreads)
	{
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(5);
		rox::core::MonotonicClock::time_point stableSince = rox::core::MonotonicClock::now();
		DWORD previousHandles = 0;
		DWORD previousThreads = 0;
		while (rox::core::MonotonicClock::now() < deadline)
		{
			DWORD handles = 0;
			if (!GetProcessHandleCount(GetCurrentProcess(), &handles))
			{
				return false;
			}
			const DWORD threads = currentThreadCount();
			if (threads == 0)
			{
				return false;
			}
			const rox::core::MonotonicClock::time_point now = rox::core::MonotonicClock::now();
			if (handles != previousHandles || threads != previousThreads)
			{
				previousHandles = handles;
				previousThreads = threads;
				stableSince = now;
			}
			else if (now - stableSince >= rox::core::Milliseconds(500))
			{
				*outHandles = handles;
				*outThreads = threads;
				return true;
			}
			rox::core::SleepFor(rox::core::Milliseconds(10));
		}
		return false;
	}

	class WindowsStandardStreamsNativeTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			ASSERT_TRUE(CreatePipe(&m_InputRead, &m_InputWrite, nullptr, 0));
			ASSERT_TRUE(CreatePipe(&m_OutputRead, &m_OutputWrite, nullptr, 0));
		}

		void TearDown() override
		{
			if (m_Owner)
			{
				closeOwner();
			}
			for (HANDLE handle : { m_InputRead, m_InputWrite, m_OutputRead, m_OutputWrite })
			{
				if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
				{
					EXPECT_TRUE(CloseHandle(handle));
				}
			}
			EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
			EXPECT_EQ(m_Memory.GetStatistics().LiveByteCount, 0);
		}

		core::Result<os::StandardStreamsOwner> createWithHandles(HANDLE input, HANDLE output, core::MemoryResource* resourceOrNull = nullptr, os::StandardStreamsOptions options = {})
		{
			const HANDLE previousInput = GetStdHandle(STD_INPUT_HANDLE);
			const HANDLE previousOutput = GetStdHandle(STD_OUTPUT_HANDLE);
			if (!SetStdHandle(STD_INPUT_HANDLE, input) || !SetStdHandle(STD_OUTPUT_HANDLE, output))
			{
				SetStdHandle(STD_INPUT_HANDLE, previousInput);
				SetStdHandle(STD_OUTPUT_HANDLE, previousOutput);
				ADD_FAILURE() << "Could not redirect standard handles";
				return core::Result<os::StandardStreamsOwner>::Failure(core::EErrorCode::ExternalFailure, "TEST-REDIRECT", "Redirection failed.");
			}
			core::Result<os::StandardStreamsOwner> result = os::WindowsStandardStreams::Create(resourceOrNull == nullptr ? m_Memory : *resourceOrNull, options);
			EXPECT_TRUE(SetStdHandle(STD_INPUT_HANDLE, previousInput));
			EXPECT_TRUE(SetStdHandle(STD_OUTPUT_HANDLE, previousOutput));
			return result;
		}

		void createOverlappedPipes()
		{
			EXPECT_TRUE(CloseHandle(m_InputRead));
			EXPECT_TRUE(CloseHandle(m_InputWrite));
			EXPECT_TRUE(CloseHandle(m_OutputRead));
			EXPECT_TRUE(CloseHandle(m_OutputWrite));
			m_InputRead = nullptr;
			m_InputWrite = nullptr;
			m_OutputRead = nullptr;
			m_OutputWrite = nullptr;
			const std::wstring prefix = L"\\\\.\\pipe\\roxell-overlapped-" + std::to_wstring(GetCurrentProcessId());
			const std::wstring inputName = prefix + L"-in";
			const std::wstring outputName = prefix + L"-out";
			m_InputRead = CreateNamedPipeW(inputName.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
			ASSERT_NE(m_InputRead, INVALID_HANDLE_VALUE);
			m_InputWrite = CreateFileW(inputName.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			ASSERT_NE(m_InputWrite, INVALID_HANDLE_VALUE);
			OVERLAPPED connection{};
			const BOOL inputConnected = ConnectNamedPipe(m_InputRead, &connection);
			ASSERT_TRUE(inputConnected || GetLastError() == ERROR_PIPE_CONNECTED);
			m_OutputRead = CreateNamedPipeW(outputName.c_str(), PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
			ASSERT_NE(m_OutputRead, INVALID_HANDLE_VALUE);
			m_OutputWrite = CreateFileW(outputName.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			ASSERT_NE(m_OutputWrite, INVALID_HANDLE_VALUE);
			const BOOL outputConnected = ConnectNamedPipe(m_OutputRead, nullptr);
			ASSERT_TRUE(outputConnected || GetLastError() == ERROR_PIPE_CONNECTED);
		}

		void closeOwner()
		{
			m_Owner->RequestClose();
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			while (!m_Owner->IsClosed() && rox::core::MonotonicClock::now() < deadline)
			{
				core::Result<bool> closed = m_Owner->PollClose();
				if (!closed)
				{
					ADD_FAILURE() << "PollClose failed";
					break;
				}
				rox::core::YieldThread();
			}
			EXPECT_TRUE(m_Owner->IsClosed());
			if (m_Owner->IsClosed())
			{
				m_Owner.Reset();
			}
		}

	protected:
		core::TrackingMemoryResource m_Memory{ core::SystemMemoryResource::Get() };
		os::StandardStreamsOwner m_Owner;
		HANDLE m_InputRead = nullptr;
		HANDLE m_InputWrite = nullptr;
		HANDLE m_OutputRead = nullptr;
		HANDLE m_OutputWrite = nullptr;
	};

	TEST_F(WindowsStandardStreamsNativeTests, MissingAndUnsupportedInputHandlesAreRejectedWithoutLeaks)
	{
		const HANDLE device = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		ASSERT_NE(device, INVALID_HANDLE_VALUE);
		{
			core::Result<os::StandardStreamsOwner> unsupported = createWithHandles(device, m_OutputWrite);
			EXPECT_TRUE(CloseHandle(device));
			ASSERT_FALSE(unsupported);
			EXPECT_EQ(unsupported.GetError().Code, core::EErrorCode::Unsupported);
			EXPECT_EQ(unsupported.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-SUPPORT");
		}
		EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
		{
			core::Result<os::StandardStreamsOwner> missing = createWithHandles(nullptr, m_OutputWrite);
			ASSERT_FALSE(missing);
			EXPECT_EQ(missing.GetError().Code, core::EErrorCode::InvalidArgument);
			EXPECT_EQ(missing.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-HANDLE");
		}
		EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST_F(WindowsStandardStreamsNativeTests, MessageAndNonblockingPipesAreRejectedWithoutLeaks)
	{
		for (const DWORD mode : { PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_TYPE_BYTE | PIPE_NOWAIT })
		{
			const std::wstring name = L"\\\\.\\pipe\\roxell-stream-refusal-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(mode);
			const HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND, mode, 1, 4096, 4096, 0, nullptr);
			ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
			{
				core::Result<os::StandardStreamsOwner> rejected = createWithHandles(pipe, m_OutputWrite);
				EXPECT_TRUE(CloseHandle(pipe));
				ASSERT_FALSE(rejected);
				EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::Unsupported);
				EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-SUPPORT");
			}
			EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
		}
	}

	TEST_F(WindowsStandardStreamsNativeTests, ReadFailureRetiresInputAndCloseReleasesResources)
	{
		core::Result<os::StandardStreamsOwner> created = createWithHandles(m_OutputWrite, m_OutputWrite);
		ASSERT_TRUE(created);
		m_Owner = core::Move(created).TakeValue();
		ASSERT_TRUE(m_Owner->ReadSome(1, m_Memory));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bFailed = false;
		while (rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(m_Owner->Poll());
			core::Result<core::Optional<os::StandardReadResult>> result = m_Owner->TakeRead();
			if (!result)
			{
				EXPECT_EQ(result.GetError().Code, core::EErrorCode::ExternalFailure);
				EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-READ");
				bFailed = true;
				break;
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bFailed);
		core::Result<void> retry = m_Owner->ReadSome(1, m_Memory);
		ASSERT_FALSE(retry);
		EXPECT_EQ(retry.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(retry.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-BUSY");
	}

	TEST_F(WindowsStandardStreamsNativeTests, RepeatedPendingReadsCloseWithoutLeakingHandles)
	{
		DWORD baseline = 0;
		DWORD baselineThreads = 0;
		for (core::int32 index = 0; index < 105; ++index)
		{
			if (index == 5)
			{
				ASSERT_TRUE(stableProcessCounts(&baseline, &baselineThreads));
			}
			core::Result<os::StandardStreamsOwner> created = createWithHandles(m_InputRead, m_OutputWrite);
			ASSERT_TRUE(created);
			m_Owner = core::Move(created).TakeValue();
			ASSERT_TRUE(m_Owner->ReadSome(1, m_Memory));
			const core::Byte byte = static_cast<core::Byte>('p');
			ASSERT_TRUE(m_Owner->Write({ &byte, 1 }));
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			bool bWritten = false;
			while (!bWritten && rox::core::MonotonicClock::now() < deadline)
			{
				ASSERT_TRUE(m_Owner->Poll());
				core::Result<core::Optional<os::StandardWriteResult>> result = m_Owner->TakeWrite();
				ASSERT_TRUE(result);
				if (result.Value().has_value())
				{
					EXPECT_EQ(result.Value()->BytesWritten, 1);
					bWritten = true;
				}
				rox::core::YieldThread();
			}
			ASSERT_TRUE(bWritten);
			m_Owner->RequestClose();
			while (!m_Owner->IsClosed() && rox::core::MonotonicClock::now() < deadline)
			{
				ASSERT_TRUE(m_Owner->PollClose());
				rox::core::YieldThread();
			}
			ASSERT_TRUE(m_Owner->IsClosed());
			{
				core::Result<core::Optional<os::StandardReadResult>> cancelled = m_Owner->TakeRead();
				ASSERT_FALSE(cancelled);
				EXPECT_EQ(cancelled.GetError().Code, core::EErrorCode::Cancelled);
				EXPECT_EQ(cancelled.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-CANCELLED");
			}
			m_Owner.Reset();
			EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
		}
		DWORD handlesAfter = 0;
		DWORD threadsAfter = 0;
		ASSERT_TRUE(stableProcessCounts(&handlesAfter, &threadsAfter));
		EXPECT_EQ(handlesAfter, baseline);
		EXPECT_EQ(threadsAfter, baselineThreads);
	}
}

namespace
{
	TEST_F(WindowsStandardStreamsNativeTests, DiskInputIsRejectedAndTemporaryFileIsRemoved)
	{
		wchar_t directory[MAX_PATH]{};
		wchar_t path[MAX_PATH]{};
		ASSERT_GT(GetTempPathW(MAX_PATH, directory), 0);
		ASSERT_NE(GetTempFileNameW(directory, L"rox", 0, path), 0);
		const HANDLE file = CreateFileW(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			DeleteFileW(path);
			FAIL() << "Could not open temporary file";
		}
		{
			core::Result<os::StandardStreamsOwner> result = createWithHandles(file, m_OutputWrite);
			EXPECT_TRUE(CloseHandle(file));
			ASSERT_FALSE(result);
			EXPECT_EQ(result.GetError().Code, core::EErrorCode::Unsupported);
			EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-SUPPORT");
		}
		EXPECT_EQ(GetFileAttributesW(path), INVALID_FILE_ATTRIBUTES);
		EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST_F(WindowsStandardStreamsNativeTests, OverlappedBytePipesTransferDataAndClose)
	{
		createOverlappedPipes();
		ASSERT_FALSE(HasFatalFailure());
		core::Result<os::StandardStreamsOwner> created = createWithHandles(m_InputRead, m_OutputWrite);
		ASSERT_TRUE(created);
		m_Owner = core::Move(created).TakeValue();
		constexpr char INPUT[] = "overlapped input";
		DWORD count = 0;
		ASSERT_TRUE(WriteFile(m_InputWrite, INPUT, sizeof(INPUT) - 1, &count, nullptr));
		ASSERT_EQ(count, sizeof(INPUT) - 1);
		ASSERT_TRUE(m_Owner->ReadSome(64, m_Memory));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bRead = false;
		while (rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(m_Owner->Poll());
			core::Result<core::Optional<os::StandardReadResult>> result = m_Owner->TakeRead();
			ASSERT_TRUE(result);
			if (result.Value().has_value())
			{
				const os::StandardReadResult& read = result.Value().value();
				EXPECT_EQ(read.Status, os::EStandardReadStatus::Data);
				EXPECT_EQ(std::string(reinterpret_cast<const char*>(read.Bytes.Data()), read.Bytes.Size()), INPUT);
				bRead = true;
				break;
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bRead);
		constexpr char OUTPUT[] = "overlapped output";
		ASSERT_TRUE(m_Owner->Write({ reinterpret_cast<const core::Byte*>(OUTPUT), sizeof(OUTPUT) - 1 }));
		bool bWritten = false;
		const rox::core::MonotonicClock::time_point writeDeadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (rox::core::MonotonicClock::now() < writeDeadline)
		{
			ASSERT_TRUE(m_Owner->Poll());
			core::Result<core::Optional<os::StandardWriteResult>> result = m_Owner->TakeWrite();
			ASSERT_TRUE(result);
			if (result.Value().has_value())
			{
				EXPECT_EQ(result.Value().value().BytesWritten, sizeof(OUTPUT) - 1);
				bWritten = true;
				break;
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bWritten);
		char received[sizeof(OUTPUT)]{};
		ASSERT_TRUE(ReadFile(m_OutputRead, received, sizeof(OUTPUT) - 1, &count, nullptr));
		EXPECT_EQ(std::string(received, count), OUTPUT);
	}
}

namespace
{
	TEST_F(WindowsStandardStreamsNativeTests, ReadResultOutlivesOwnerAndKeepsCallerResource)
	{
		core::TrackingMemoryResource resultMemory(core::SystemMemoryResource::Get());
		{
			core::Result<os::StandardStreamsOwner> created = createWithHandles(m_InputRead, m_OutputWrite);
			ASSERT_TRUE(created);
			m_Owner = core::Move(created).TakeValue();
			constexpr char INPUT[] = "retained bytes";
			DWORD written = 0;
			ASSERT_TRUE(WriteFile(m_InputWrite, INPUT, sizeof(INPUT) - 1, &written, nullptr));
			ASSERT_EQ(written, sizeof(INPUT) - 1);
			ASSERT_TRUE(m_Owner->ReadSome(64, resultMemory));
			core::Optional<os::StandardReadResult> retained;
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			while (!retained.has_value() && rox::core::MonotonicClock::now() < deadline)
			{
				ASSERT_TRUE(m_Owner->Poll());
				core::Result<core::Optional<os::StandardReadResult>> result = m_Owner->TakeRead();
				ASSERT_TRUE(result);
				retained = core::Move(result).TakeValue();
				rox::core::YieldThread();
			}
			ASSERT_TRUE(retained.has_value());
			closeOwner();
			ASSERT_FALSE(m_Owner);
			EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
			EXPECT_GT(resultMemory.GetStatistics().LiveAllocationCount, 0);
			EXPECT_EQ(std::string(reinterpret_cast<const char*>(retained->Bytes.Data()), retained->Bytes.Size()), INPUT);
			DWORD flags = 0;
			EXPECT_TRUE(GetHandleInformation(m_InputRead, &flags));
			EXPECT_TRUE(GetHandleInformation(m_OutputWrite, &flags));
		}
		EXPECT_EQ(resultMemory.GetStatistics().LiveAllocationCount, 0);
		EXPECT_EQ(resultMemory.GetStatistics().LiveByteCount, 0);
	}

	TEST_F(WindowsStandardStreamsNativeTests, CreationAllocationFailuresPreserveHandlesAndDoNotLeak)
	{
		DWORD before = 0;
		DWORD threadsBefore = 0;
		ASSERT_TRUE(stableProcessCounts(&before, &threadsBefore));
		for (core::usize allowed = 0; allowed < 2; ++allowed)
		{
			core::FailingMemoryResource failing(m_Memory, allowed);
			{
				core::Result<os::StandardStreamsOwner> result = createWithHandles(m_InputRead, m_OutputWrite, &failing);
				ASSERT_FALSE(result);
				EXPECT_EQ(result.GetError().Code, core::EErrorCode::ExternalFailure);
				EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-ALLOCATION");
			}
			EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
		}
		DWORD after = 0;
		DWORD threadsAfter = 0;
		ASSERT_TRUE(stableProcessCounts(&after, &threadsAfter));
		EXPECT_EQ(after, before);
		EXPECT_EQ(threadsAfter, threadsBefore);
		core::Result<os::StandardStreamsOwner> created = createWithHandles(m_InputRead, m_OutputWrite);
		ASSERT_TRUE(created);
		m_Owner = core::Move(created).TakeValue();
	}
}

namespace
{
	TEST_F(WindowsStandardStreamsNativeTests, InvalidCreationBoundsDoNotPublishOwnerOrLeak)
	{
		for (const core::usize invalid : { core::usize(0), os::MAXIMUM_STANDARD_STREAM_BYTES + 1 })
		{
			for (const bool bRead : { true, false })
			{
				os::StandardStreamsOptions options;
				if (bRead)
				{
					options.MaximumReadBytes = invalid;
				}
				else
				{
					options.MaximumWriteBytes = invalid;
				}
				{
					core::Result<os::StandardStreamsOwner> result = createWithHandles(m_InputRead, m_OutputWrite, nullptr, options);
					ASSERT_FALSE(result);
					EXPECT_EQ(result.GetError().Code, core::EErrorCode::InvalidArgument);
					EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-BOUNDS");
				}
				EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
			}
		}
		core::Result<os::StandardStreamsOwner> created = createWithHandles(m_InputRead, m_OutputWrite);
		ASSERT_TRUE(created);
		m_Owner = core::Move(created).TakeValue();
	}

	TEST_F(WindowsStandardStreamsNativeTests, RequestsAboveConfiguredBoundsDoNotOccupyDirections)
	{
		core::Result<os::StandardStreamsOwner> created = createWithHandles(m_InputRead, m_OutputWrite);
		ASSERT_TRUE(created);
		m_Owner = core::Move(created).TakeValue();
		core::Result<void> read = m_Owner->ReadSome(os::DEFAULT_STANDARD_READ_BYTES + 1, m_Memory);
		ASSERT_FALSE(read);
		EXPECT_EQ(read.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(read.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-BOUNDS");
		std::vector<core::Byte> bytes(os::DEFAULT_STANDARD_WRITE_BYTES + 1);
		core::Result<void> write = m_Owner->Write({ bytes.data(), bytes.size() });
		ASSERT_FALSE(write);
		EXPECT_EQ(write.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(write.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-BOUNDS");
		core::Result<core::Optional<os::StandardReadResult>> noRead = m_Owner->TakeRead();
		core::Result<core::Optional<os::StandardWriteResult>> noWrite = m_Owner->TakeWrite();
		ASSERT_FALSE(noRead);
		ASSERT_FALSE(noWrite);
		EXPECT_EQ(noRead.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-NO-READ");
		EXPECT_EQ(noWrite.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-NO-WRITE");
	}
}

namespace
{
	TEST_F(WindowsStandardStreamsNativeTests, CancellingOverlappedReadRetiresPendingOperation)
	{
		createOverlappedPipes();
		ASSERT_FALSE(HasFatalFailure());
		core::Result<os::StandardStreamsOwner> created = createWithHandles(m_InputRead, m_OutputWrite);
		ASSERT_TRUE(created);
		m_Owner = core::Move(created).TakeValue();
		core::CancellationSource cancellation;
		ASSERT_TRUE(cancellation.IsValid());
		ASSERT_TRUE(m_Owner->ReadSome(64, m_Memory, cancellation.GetToken()));
		cancellation.RequestCancellation();
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bCancelled = false;
		while (rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(m_Owner->Poll());
			core::Result<core::Optional<os::StandardReadResult>> result = m_Owner->TakeRead();
			if (!result)
			{
				EXPECT_EQ(result.GetError().Code, core::EErrorCode::Cancelled);
				EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-CANCELLED");
				bCancelled = true;
				break;
			}
			EXPECT_FALSE(result.Value().has_value());
			rox::core::YieldThread();
		}
		EXPECT_TRUE(bCancelled);
	}

	TEST_F(WindowsStandardStreamsNativeTests, CancellingBackpressuredOverlappedWriteRetiresOutput)
	{
		createOverlappedPipes();
		ASSERT_FALSE(HasFatalFailure());
		core::Result<os::StandardStreamsOwner> created = createWithHandles(m_InputRead, m_OutputWrite);
		ASSERT_TRUE(created);
		m_Owner = core::Move(created).TakeValue();
		core::CancellationSource cancellation;
		ASSERT_TRUE(cancellation.IsValid());
		std::vector<core::Byte> bytes(os::DEFAULT_STANDARD_WRITE_BYTES, static_cast<core::Byte>('z'));
		ASSERT_TRUE(m_Owner->Write({ bytes.data(), bytes.size() }, cancellation.GetToken()));
		cancellation.RequestCancellation();
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bCancelled = false;
		while (rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(m_Owner->Poll());
			core::Result<core::Optional<os::StandardWriteResult>> result = m_Owner->TakeWrite();
			if (!result)
			{
				EXPECT_EQ(result.GetError().Code, core::EErrorCode::Cancelled);
				EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-CANCELLED");
				bCancelled = true;
				break;
			}
			EXPECT_FALSE(result.Value().has_value());
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bCancelled);
		const core::Byte byte = static_cast<core::Byte>('x');
		core::Result<void> retry = m_Owner->Write({ &byte, 1 });
		ASSERT_FALSE(retry);
		EXPECT_EQ(retry.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(retry.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-BUSY");
	}
}

namespace
{
	TEST_F(WindowsStandardStreamsNativeTests, LargeWritePreservesEveryByteAfterSourceDestruction)
	{
		core::Result<os::StandardStreamsOwner> created = createWithHandles(m_InputRead, m_OutputWrite);
		ASSERT_TRUE(created);
		m_Owner = core::Move(created).TakeValue();
		constexpr core::usize LENGTH = 131073;
		{
			std::vector<core::Byte> source(LENGTH);
			for (core::usize index = 0; index < LENGTH; ++index)
			{
				source[index] = static_cast<core::Byte>(index % 251);
			}
			ASSERT_TRUE(m_Owner->Write({ source.data(), source.size() }));
		}
		std::vector<core::Byte> received;
		bool bCompleted = false;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while ((!bCompleted || received.size() < LENGTH) && rox::core::MonotonicClock::now() < deadline)
		{
			DWORD available = 0;
			ASSERT_TRUE(PeekNamedPipe(m_OutputRead, nullptr, 0, nullptr, &available, nullptr));
			if (available > 0)
			{
				core::Byte chunk[4096]{};
				DWORD count = 0;
				const DWORD request = available < sizeof(chunk) ? available : static_cast<DWORD>(sizeof(chunk));
				ASSERT_TRUE(ReadFile(m_OutputRead, chunk, request, &count, nullptr));
				received.insert(received.end(), chunk, chunk + count);
			}
			ASSERT_TRUE(m_Owner->Poll());
			if (!bCompleted)
			{
				core::Result<core::Optional<os::StandardWriteResult>> result = m_Owner->TakeWrite();
				ASSERT_TRUE(result);
				if (result.Value().has_value())
				{
					EXPECT_EQ(result.Value()->BytesWritten, LENGTH);
					bCompleted = true;
				}
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bCompleted);
		ASSERT_EQ(received.size(), LENGTH);
		for (core::usize index = 0; index < LENGTH; ++index)
		{
			ASSERT_EQ(received[index], static_cast<core::Byte>(index % 251)) << "byte " << index;
		}
	}
}
