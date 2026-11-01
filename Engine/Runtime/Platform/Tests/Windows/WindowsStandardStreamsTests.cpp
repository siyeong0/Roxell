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

#include <string>
#include <thread>
#include <vector>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	class WindowsStandardStreamsTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			ASSERT_TRUE(CreatePipe(&inputRead, &inputWrite, nullptr, 0));
			ASSERT_TRUE(CreatePipe(&outputRead, &outputWrite, nullptr, 0));
			const HANDLE previousInput = GetStdHandle(STD_INPUT_HANDLE);
			const HANDLE previousOutput = GetStdHandle(STD_OUTPUT_HANDLE);
			ASSERT_TRUE(SetStdHandle(STD_INPUT_HANDLE, inputRead));
			if (!SetStdHandle(STD_OUTPUT_HANDLE, outputWrite))
			{
				SetStdHandle(STD_INPUT_HANDLE, previousInput);
				FAIL() << "Could not redirect standard output";
			}
			core::Result<os::StandardStreamsOwner> created = os::WindowsStandardStreams::Create(failing);
			const BOOL bInputRestored = SetStdHandle(STD_INPUT_HANDLE, previousInput);
			const BOOL bOutputRestored = SetStdHandle(STD_OUTPUT_HANDLE, previousOutput);
			ASSERT_TRUE(bInputRestored);
			ASSERT_TRUE(bOutputRestored);
			ASSERT_TRUE(created);
			owner = core::Move(created).TakeValue();
		}

		void TearDown() override
		{
			if (owner)
			{
				failing.Reset(32);
				owner->RequestClose();
				const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
				while (!owner->IsClosed() && rox::core::MonotonicClock::now() < deadline)
				{
					core::Result<bool> closed = owner->PollClose();
					if (!closed)
					{
						ADD_FAILURE() << "PollClose failed";
						break;
					}
					rox::core::YieldThread();
				}
				EXPECT_TRUE(owner->IsClosed());
				if (owner->IsClosed())
				{
					owner.Reset();
				}
			}
			for (HANDLE handle : { inputRead, inputWrite, outputRead, outputWrite })
			{
				if (handle != nullptr)
				{
					EXPECT_TRUE(CloseHandle(handle));
				}
			}
			EXPECT_EQ(resource.GetStatistics().LiveAllocationCount, 0u);
			EXPECT_EQ(resource.GetStatistics().LiveByteCount, 0u);
		}

	protected:
		core::TrackingMemoryResource resource{ core::SystemMemoryResource::Get() };
		core::FailingMemoryResource failing{ resource, 32 };
		os::StandardStreamsOwner owner;
		HANDLE inputRead = nullptr;
		HANDLE inputWrite = nullptr;
		HANDLE outputRead = nullptr;
		HANDLE outputWrite = nullptr;
	};

	TEST_F(WindowsStandardStreamsTests, EmptyReadsAndWritesAreRejectedWithoutAdmittingOperations)
	{
		const core::Result<void> read = owner->ReadSome(0, resource);
		ASSERT_FALSE(read);
		EXPECT_EQ(read.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(read.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-BOUNDS");
		const core::Result<void> write = owner->Write({});
		ASSERT_FALSE(write);
		EXPECT_EQ(write.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(write.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-BOUNDS");
		const core::Result<core::Optional<os::StandardReadResult>> noRead = owner->TakeRead();
		const core::Result<core::Optional<os::StandardWriteResult>> noWrite = owner->TakeWrite();
		ASSERT_FALSE(noRead);
		ASSERT_FALSE(noWrite);
		EXPECT_EQ(noRead.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(noRead.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-NO-READ");
		EXPECT_EQ(noWrite.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(noWrite.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-NO-WRITE");
	}

	TEST_F(WindowsStandardStreamsTests, ReadsBytesFromRedirectedInput)
	{
		constexpr char TEXT[] = "input bytes";
		DWORD written = 0;
		ASSERT_TRUE(WriteFile(inputWrite, TEXT, sizeof(TEXT) - 1, &written, nullptr));
		ASSERT_EQ(written, sizeof(TEXT) - 1);
		ASSERT_TRUE(owner->ReadSome(32, resource));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bCompleted = false;
		while (!bCompleted && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->Poll());
			core::Result<core::Optional<os::StandardReadResult>> result = owner->TakeRead();
			ASSERT_TRUE(result);
			if (result.Value().has_value())
			{
				const os::StandardReadResult& read = result.Value().value();
				EXPECT_EQ(read.Status, os::EStandardReadStatus::Data);
				const std::string actual(reinterpret_cast<const char*>(read.Bytes.Data()), read.Bytes.Size());
				EXPECT_EQ(actual, TEXT);
				bCompleted = true;
			}
			rox::core::YieldThread();
		}
		EXPECT_TRUE(bCompleted);
	}
	TEST_F(WindowsStandardStreamsTests, WritesBytesToRedirectedOutput)
	{
		constexpr char TEXT[] = "output bytes";
		const core::Byte* bytes = reinterpret_cast<const core::Byte*>(TEXT);
		ASSERT_TRUE(owner->Write({ bytes, sizeof(TEXT) - 1 }));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bCompleted = false;
		while (!bCompleted && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->Poll());
			core::Result<core::Optional<os::StandardWriteResult>> result = owner->TakeWrite();
			ASSERT_TRUE(result);
			if (result.Value().has_value())
			{
				EXPECT_EQ(result.Value().value().BytesWritten, sizeof(TEXT) - 1);
				bCompleted = true;
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bCompleted);
		char received[sizeof(TEXT)] = {};
		DWORD read = 0;
		ASSERT_TRUE(ReadFile(outputRead, received, sizeof(TEXT) - 1, &read, nullptr));
		EXPECT_EQ(read, sizeof(TEXT) - 1);
		EXPECT_EQ(std::string(received, read), TEXT);
	}

	TEST_F(WindowsStandardStreamsTests, FailedReadAllocationDoesNotAdmitOperation)
	{
		core::FailingMemoryResource failingResultResource(resource, 0);
		const core::Result<void> refused = owner->ReadSome(32, failingResultResource);
		ASSERT_FALSE(refused);
		EXPECT_EQ(refused.GetError().Code, core::EErrorCode::ExternalFailure);
		EXPECT_EQ(refused.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-ALLOCATION");
		const core::Result<core::Optional<os::StandardReadResult>> noRead = owner->TakeRead();
		ASSERT_FALSE(noRead);
		EXPECT_EQ(noRead.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(noRead.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-NO-READ");
		constexpr char BYTE = 'x';
		DWORD written = 0;
		ASSERT_TRUE(WriteFile(inputWrite, &BYTE, 1, &written, nullptr));
		ASSERT_EQ(written, 1u);
		ASSERT_TRUE(owner->ReadSome(1, resource));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bCompleted = false;
		while (!bCompleted && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->Poll());
			core::Result<core::Optional<os::StandardReadResult>> result = owner->TakeRead();
			ASSERT_TRUE(result);
			if (result.Value().has_value())
			{
				const core::Array<core::Byte>& bytes = result.Value()->Bytes;
				ASSERT_EQ(bytes.Size(), 1u);
				EXPECT_EQ(bytes[0], static_cast<core::Byte>('x'));
				bCompleted = true;
			}
			rox::core::YieldThread();
		}
		EXPECT_TRUE(bCompleted);
	}

	TEST_F(WindowsStandardStreamsTests, CancellingPendingReadRetiresOperation)
	{
		core::CancellationSource cancellation;
		ASSERT_TRUE(cancellation.IsValid());
		ASSERT_TRUE(owner->ReadSome(32, resource, cancellation.GetToken()));
		cancellation.RequestCancellation();
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bCancelled = false;
		while (!bCancelled && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->Poll());
			core::Result<core::Optional<os::StandardReadResult>> result = owner->TakeRead();
			if (!result)
			{
				EXPECT_EQ(result.GetError().Code, core::EErrorCode::Cancelled);
				EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-CANCELLED");
				bCancelled = true;
			}
			else
			{
				EXPECT_FALSE(result.Value().has_value());
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bCancelled);
		const core::Result<core::Optional<os::StandardReadResult>> consumed = owner->TakeRead();
		ASSERT_FALSE(consumed);
		EXPECT_EQ(consumed.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(consumed.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-NO-READ");
	}

	TEST_F(WindowsStandardStreamsTests, BrokenOutputPipeReportsFailureAndRetiresStream)
	{
		ASSERT_TRUE(CloseHandle(outputRead));
		outputRead = nullptr;
		const core::Byte byte = static_cast<core::Byte>('x');
		ASSERT_TRUE(owner->Write({ &byte, 1 }));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bFailed = false;
		while (!bFailed && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->Poll());
			core::Result<core::Optional<os::StandardWriteResult>> result = owner->TakeWrite();
			if (!result)
			{
				EXPECT_EQ(result.GetError().Code, core::EErrorCode::ExternalFailure);
				EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-WRITE");
				bFailed = true;
			}
			else
			{
				EXPECT_FALSE(result.Value().has_value());
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bFailed);
		const core::Result<void> retry = owner->Write({ &byte, 1 });
		ASSERT_FALSE(retry);
		EXPECT_EQ(retry.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(retry.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-BUSY");
	}

	TEST_F(WindowsStandardStreamsTests, ForeignThreadUseTriggersOwnershipAssertion)
	{
		ASSERT_DEATH(
			{
				std::thread foreign([this]() { (void)owner->Poll(); });
				foreign.join();
			},
			"Standard streams must be operated on their creating thread");
	}

	TEST_F(WindowsStandardStreamsTests, DestroyingUnclosedOwnerTriggersOwnershipAssertion)
	{
		ASSERT_DEATH(owner.Reset(), "Standard streams require completed close before owner destruction");
	}

	TEST_F(WindowsStandardStreamsTests, ClosedInputProducesEndOfStream)
	{
		ASSERT_TRUE(CloseHandle(inputWrite));
		inputWrite = nullptr;
		ASSERT_TRUE(owner->ReadSome(32, resource));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bCompleted = false;
		while (!bCompleted && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->Poll());
			core::Result<core::Optional<os::StandardReadResult>> result = owner->TakeRead();
			ASSERT_TRUE(result);
			if (result.Value().has_value())
			{
				EXPECT_EQ(result.Value()->Status, os::EStandardReadStatus::EndOfStream);
				EXPECT_TRUE(result.Value()->Bytes.IsEmpty());
				bCompleted = true;
			}
			rox::core::YieldThread();
		}
		EXPECT_TRUE(bCompleted);
	}

	TEST_F(WindowsStandardStreamsTests, CloseCancelsPendingReadAndRetainsCompletion)
	{
		ASSERT_TRUE(owner->ReadSome(32, resource));
		owner->RequestClose();
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!owner->IsClosed() && rox::core::MonotonicClock::now() < deadline)
		{
			const core::Result<bool> closed = owner->PollClose();
			ASSERT_TRUE(closed);
			rox::core::YieldThread();
		}
		ASSERT_TRUE(owner->IsClosed());
		const core::Result<core::Optional<os::StandardReadResult>> cancelled = owner->TakeRead();
		ASSERT_FALSE(cancelled);
		EXPECT_EQ(cancelled.GetError().Code, core::EErrorCode::Cancelled);
		EXPECT_EQ(cancelled.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-CANCELLED");
		const core::Result<core::Optional<os::StandardReadResult>> consumed = owner->TakeRead();
		ASSERT_FALSE(consumed);
		EXPECT_EQ(consumed.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-NO-READ");
	}

	TEST_F(WindowsStandardStreamsTests, PrecancelledOperationsDoNotOccupyDirections)
	{
		core::CancellationSource cancellation;
		ASSERT_TRUE(cancellation.IsValid());
		cancellation.RequestCancellation();
		const core::Result<void> read = owner->ReadSome(1, resource, cancellation.GetToken());
		ASSERT_FALSE(read);
		EXPECT_EQ(read.GetError().Code, core::EErrorCode::Cancelled);
		EXPECT_EQ(read.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-CANCELLED");
		const core::Byte byte = static_cast<core::Byte>('x');
		const core::Result<void> write = owner->Write({ &byte, 1 }, cancellation.GetToken());
		ASSERT_FALSE(write);
		EXPECT_EQ(write.GetError().Code, core::EErrorCode::Cancelled);
		EXPECT_EQ(write.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-CANCELLED");
		ASSERT_TRUE(owner->ReadSome(1, resource));
		ASSERT_TRUE(owner->Write({ &byte, 1 }));
	}

	TEST_F(WindowsStandardStreamsTests, WriteCopiesCallerBytesBeforeReturning)
	{
		char source[] = "original";
		ASSERT_TRUE(owner->Write({ reinterpret_cast<const core::Byte*>(source), sizeof(source) - 1 }));
		source[0] = 'X';
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bCompleted = false;
		while (!bCompleted && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->Poll());
			core::Result<core::Optional<os::StandardWriteResult>> result = owner->TakeWrite();
			ASSERT_TRUE(result);
			if (result.Value().has_value())
			{
				EXPECT_EQ(result.Value()->BytesWritten, sizeof(source) - 1);
				bCompleted = true;
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bCompleted);
		char received[sizeof(source)] = {};
		DWORD read = 0;
		ASSERT_TRUE(ReadFile(outputRead, received, sizeof(source) - 1, &read, nullptr));
		EXPECT_EQ(read, sizeof(source) - 1);
		EXPECT_EQ(std::string(received, read), "original");
	}

	TEST_F(WindowsStandardStreamsTests, WriteAllocationFailuresDoNotOccupyOutputDirection)
	{
		const core::Byte byte = static_cast<core::Byte>('x');
		for (core::usize attempts = 0; attempts < 2; ++attempts)
		{
			failing.Reset(attempts);
			core::Result<void> refused = owner->Write({ &byte, 1 });
			ASSERT_FALSE(refused);
			EXPECT_EQ(refused.GetError().Code, core::EErrorCode::ExternalFailure);
			failing.Reset(32);
			const core::Result<core::Optional<os::StandardWriteResult>> noWrite = owner->TakeWrite();
			ASSERT_FALSE(noWrite);
			EXPECT_EQ(noWrite.GetError().Code, core::EErrorCode::Conflict);
			EXPECT_EQ(noWrite.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-NO-WRITE");
		}
		ASSERT_TRUE(owner->Write({ &byte, 1 }));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		bool bCompleted = false;
		while (!bCompleted && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->Poll());
			core::Result<core::Optional<os::StandardWriteResult>> result = owner->TakeWrite();
			ASSERT_TRUE(result);
			if (result.Value().has_value())
			{
				EXPECT_EQ(result.Value()->BytesWritten, 1u);
				bCompleted = true;
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bCompleted);
		char received = 0;
		DWORD bytesRead = 0;
		ASSERT_TRUE(ReadFile(outputRead, &received, 1, &bytesRead, nullptr));
		EXPECT_EQ(bytesRead, 1u);
		EXPECT_EQ(received, 'x');
	}

	TEST_F(WindowsStandardStreamsTests, CancellingBackpressuredWriteRetiresOutput)
	{
		const std::vector<core::Byte> bytes(256 * 1024, static_cast<core::Byte>('x'));
		core::CancellationSource cancellation;
		ASSERT_TRUE(cancellation.IsValid());
		ASSERT_TRUE(owner->Write({ bytes.data(), bytes.size() }, cancellation.GetToken()));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		DWORD available = 0;
		while (available == 0 && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(PeekNamedPipe(outputRead, nullptr, 0, nullptr, &available, nullptr));
			rox::core::YieldThread();
		}
		ASSERT_GT(available, 0u);
		cancellation.RequestCancellation();
		bool bCancelled = false;
		while (!bCancelled && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->Poll());
			core::Result<core::Optional<os::StandardWriteResult>> result = owner->TakeWrite();
			if (!result)
			{
				EXPECT_EQ(result.GetError().Code, core::EErrorCode::Cancelled);
				EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-CANCELLED");
				bCancelled = true;
			}
			else
			{
				EXPECT_FALSE(result.Value().has_value());
			}
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bCancelled);
		const core::Result<void> retry = owner->Write({ bytes.data(), 1 });
		ASSERT_FALSE(retry);
		EXPECT_EQ(retry.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(retry.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-BUSY");
	}

	TEST_F(WindowsStandardStreamsTests, CloseJoinsBackpressuredWriteAndPreservesCancellation)
	{
		const std::vector<core::Byte> bytes(256 * 1024, static_cast<core::Byte>('x'));
		ASSERT_TRUE(owner->Write({ bytes.data(), bytes.size() }));
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		DWORD available = 0;
		while (available == 0 && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(PeekNamedPipe(outputRead, nullptr, 0, nullptr, &available, nullptr));
			rox::core::YieldThread();
		}
		ASSERT_GT(available, 0u);
		owner->RequestClose();
		while (!owner->IsClosed() && rox::core::MonotonicClock::now() < deadline)
		{
			ASSERT_TRUE(owner->PollClose());
			rox::core::YieldThread();
		}
		ASSERT_TRUE(owner->IsClosed());
		const core::Result<core::Optional<os::StandardWriteResult>> cancelled = owner->TakeWrite();
		ASSERT_FALSE(cancelled);
		EXPECT_EQ(cancelled.GetError().Code, core::EErrorCode::Cancelled);
		EXPECT_EQ(cancelled.GetError().DiagnosticID, "ROX-PLATFORM-STREAM-CANCELLED");
	}

} // namespace
