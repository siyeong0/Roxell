#include <Utility.hpp>
#include <Types.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
// The first include is the contract: IFileTailReader.h must compile with no prior include.
#include <IFileTailReader.h>
#include <gtest/gtest.h>
#include <TextPrimitives.hpp>
#include <StringView.hpp>

#include <WindowsFileTailReader.h>

#include <FailingMemoryResource.h>
#include <MemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;


	// The writer under test is the one the log sink uses: OPEN_ALWAYS with FILE_APPEND_DATA and read/write
	// sharing (Engine/Runtime/Platform/Private/Windows/WindowsLogSink.cpp:42-43). Tailing a file some other
	// API happened to write would prove nothing about the sink this reader exists for.
	class AppendingWriter final
	{
	public:
		[[nodiscard]] bool Open(const std::filesystem::path& path)
		{
			m_File = CreateFileW(path.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			return m_File != INVALID_HANDLE_VALUE;
		}

		~AppendingWriter()
		{
			Close();
		}

		AppendingWriter() = default;
		AppendingWriter(const AppendingWriter&) = delete;
		AppendingWriter& operator=(const AppendingWriter&) = delete;

		[[nodiscard]] bool Append(const rox::core::StringView text)
		{
			DWORD written = 0;
			return WriteFile(m_File, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != FALSE && written == text.size();
		}

		void Close()
		{
			if (m_File != INVALID_HANDLE_VALUE)
			{
				CloseHandle(m_File);
				m_File = INVALID_HANDLE_VALUE;
			}
		}

	private:
		HANDLE m_File = INVALID_HANDLE_VALUE;
	};

	[[nodiscard]] std::string toText(const core::Span<const core::Byte> bytes)
	{
		return std::string(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
	}

	// Read is nonblocking, so an empty view means "the operating system has not finished this read yet"
	// and not "nothing was appended". Every contract below that expects bytes therefore harvests until
	// the reader produces them or a deadline passes. A caller in a frame loop does the same thing across
	// frames; this loop is what that looks like when a test cannot wait a frame.
	struct HarvestResult final
	{
		std::string Text;
		bool bFailed = false;
		std::string DiagnosticID;
		int EmptyRounds = 0;
	};

	[[nodiscard]] HarvestResult harvestUntilBytes(os::WindowsFileTailReader& reader, const bool bExpectBytes)
	{
		HarvestResult result;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);

		for (;;)
		{
			rox::core::Result<rox::core::Span<const rox::core::Byte>> batch = reader.Read();

			if (!batch)
			{
				result.bFailed = true;
				result.DiagnosticID = std::string(batch.GetError().DiagnosticID.CStr());
				return result;
			}

			if (!batch.Value().IsEmpty())
			{
				result.Text = toText(batch.Value());
				return result;
			}
			++result.EmptyRounds;

			if (!bExpectBytes || rox::core::MonotonicClock::now() >= deadline)
			{
				return result;
			}

			rox::core::YieldThread();
		}
	}

	// Identity must be published before RequestIdentityProbe returns; waiting here would hide that contract.
	[[nodiscard]] os::EFileTailIdentity askIdentity(os::WindowsFileTailReader& reader)
	{
		reader.RequestIdentityProbe();
		return reader.ReadIdentity();
	}

	// Counts allocation calls, which MemoryStatistics does not: the contract below is about how many
	// times a repeated refusal reaches the allocator at all, not about how much is live afterwards.
	class CountingMemoryResource final : public core::MemoryResource
	{
	public:
		explicit CountingMemoryResource(core::MemoryResource& upstream) noexcept
			: m_Upstream(upstream)
		{
		}

		[[nodiscard]] rox::core::usize GetAllocationCallCount() const noexcept
		{
			return m_AllocationCallCount;
		}

		[[nodiscard]] rox::core::DiagnosticText GetName() const noexcept override
		{
			return "RoxellPlatformFileTailCountingResource";
		}

		[[nodiscard]] core::EMemoryResourceThreadSafety GetThreadSafety() const noexcept override
		{
			return m_Upstream.GetThreadSafety();
		}

	private:
		void* allocate(const core::MemoryAllocationRequest allocation) noexcept override
		{
			++m_AllocationCallCount;
			return m_Upstream.Allocate(allocation);
		}

		void deallocate(void* const pointer, const core::MemoryAllocationRequest allocation) noexcept override
		{
			m_Upstream.Deallocate(pointer, allocation);
		}

	private:
		core::MemoryResource& m_Upstream;
		rox::core::usize m_AllocationCallCount = 0;
	};

	// std::filesystem::path::string() returns an owner. This holds it for the duration of the call
	// expression and converts to the borrowed view the Platform API takes.
	class PathText final
	{
	public:
		explicit PathText(const std::filesystem::path& path)
			: m_Text(path.string())
		{
		}

		operator core::StringView() const noexcept
		{
			return core::StringView(m_Text.data(), m_Text.size());
		}

	private:
		std::string m_Text;
	};

	class WindowsFileTailReaderTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			const rox::core::MonotonicClock::duration::rep nonce = rox::core::MonotonicClock::now().time_since_epoch().count();
			root = std::filesystem::temp_directory_path() / ("RoxellTailTests-" + std::to_string(nonce));
			std::filesystem::create_directories(root);
		}

		void TearDown() override
		{
			EXPECT_EQ(resource.GetStatistics().LiveAllocationCount, 0u);
			EXPECT_EQ(resource.GetStatistics().LiveByteCount, 0u);
			std::error_code error;
			std::filesystem::remove_all(root, error);
			EXPECT_FALSE(error);
		}

	protected:
		core::TrackingMemoryResource resource{ core::SystemMemoryResource::Get() };
		std::filesystem::path root;
	};

	TEST_F(WindowsFileTailReaderTests, ReadsInitialBytesAndSubsequentAppends)
	{
		const std::filesystem::path file = root / "appended.jsonl";
		AppendingWriter writer;
		ASSERT_TRUE(writer.Open(file));
		ASSERT_TRUE(writer.Append("first\n"));

		rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(file));
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();
		ASSERT_TRUE(reader.IsValid());
		// The file is shorter than the backfill window, so the reader begins at byte zero and the caller must
		// not resynchronize: the first line of this file is a whole line.
		ASSERT_TRUE(!reader.BeganMidFile());

		const HarvestResult first = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!first.bFailed);
		ASSERT_TRUE(first.Text == "first\n");

		// Nothing appended is an empty view and never a failure. Asked once, because "nothing yet" and
		// "the answer has not arrived" are both empty and only the reader can tell them apart.
		rox::core::Result<rox::core::Span<const rox::core::Byte>> idle = reader.Read();
		ASSERT_TRUE(idle);
		ASSERT_TRUE(idle.Value().IsEmpty());
		ASSERT_TRUE(!reader.HasPendingBackfill());

		ASSERT_TRUE(writer.Append("second\nthi"));
		const HarvestResult second = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!second.bFailed);
		ASSERT_TRUE(second.Text == "second\nthi");

		// The partial trailing line belongs to the caller: the reader reports the rest of it on a read
		// after the writer completes it, and never re-reports the part it already returned.
		ASSERT_TRUE(writer.Append("rd\n"));
		const HarvestResult third = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!third.bFailed);
		ASSERT_TRUE(third.Text == "rd\n");
	}

	TEST_F(WindowsFileTailReaderTests, StartsAtBoundedBackfillWindow)
	{
		const std::filesystem::path file = root / "backfill.jsonl";
		AppendingWriter writer;
		ASSERT_TRUE(writer.Open(file));
		// One mebibyte of fixed-width lines, so the exact backfill boundary is computable rather than guessed.
		std::string line(63, 'a');
		line.push_back('\n');
		constexpr rox::core::usize LINE_COUNT = 1'048'576 / 64;

		for (rox::core::usize index = 0; index < LINE_COUNT; ++index)
		{
			ASSERT_TRUE(writer.Append(line));
		}

		os::WindowsFileTailReaderOptions options;
		options.MaximumBytesPerRead = os::MAXIMUM_FILE_TAIL_READ_BYTES;
		rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(file), options);
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();
		ASSERT_TRUE(reader.BeganMidFile());

		// harvested to a bounded completion, because a direct Read can legitimately return empty while
		// the operating system is still finishing it.
		const HarvestResult first = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!first.bFailed);
		ASSERT_TRUE(first.Text.size() == os::DEFAULT_FILE_TAIL_BACKFILL_BYTES);
		// The backfill boundary lands on a 64-byte line boundary here, which is the uninteresting case; the
		// interesting one is that the reader reports it began mid-file at all, because that is what tells a
		// line-oriented caller to discard whatever precedes its first separator.
		rox::core::Result<rox::core::Span<const rox::core::Byte>> idle = reader.Read();
		ASSERT_TRUE(idle);
		ASSERT_TRUE(idle.Value().IsEmpty());
	}

	TEST_F(WindowsFileTailReaderTests, ReportsBacklogUntilReadLimitIsDrained)
	{
		const std::filesystem::path file = root / "ceiling.jsonl";
		AppendingWriter writer;
		ASSERT_TRUE(writer.Open(file));

		os::WindowsFileTailReaderOptions options;
		options.MaximumBytesPerRead = 64;
		options.BackfillBytes = 0;
		rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(file), options);
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();
		ASSERT_TRUE(!reader.BeganMidFile());

		const std::string payload(200, 'x');
		ASSERT_TRUE(writer.Append(payload));

		const HarvestResult first = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!first.bFailed && first.Text.size() == 64);
		ASSERT_TRUE(reader.IsBacklogged());
		const HarvestResult second = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!second.bFailed && second.Text.size() == 64);
		ASSERT_TRUE(reader.IsBacklogged());
		const HarvestResult third = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!third.bFailed && third.Text.size() == 64);
		const HarvestResult fourth = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!fourth.bFailed && fourth.Text.size() == 8);
		ASSERT_TRUE(!reader.IsBacklogged());
	}

	TEST_F(WindowsFileTailReaderTests, TruncationReturnsPersistentGoneError)
	{
		const std::filesystem::path file = root / "truncated.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(file));
			ASSERT_TRUE(writer.Append("alpha\nbeta\n"));
		}

		rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(file));
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();
		const HarvestResult first = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!first.bFailed);
		ASSERT_TRUE(first.Text == "alpha\nbeta\n");

		{
			const HANDLE file32 = CreateFileW(file.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
				TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			ASSERT_TRUE(file32 != INVALID_HANDLE_VALUE);
			CloseHandle(file32);
		}

		const HarvestResult shrunk = harvestUntilBytes(reader, false);
		ASSERT_TRUE(shrunk.bFailed);
		ASSERT_TRUE(shrunk.DiagnosticID == "ROX-PLATFORM-TAIL-GONE");
		// Terminal means terminal: the reader never rearms, exactly as the watcher does not.
		const HarvestResult again = harvestUntilBytes(reader, false);
		ASSERT_TRUE(again.bFailed);
		ASSERT_TRUE(again.DiagnosticID == "ROX-PLATFORM-TAIL-GONE");
	}

	// Probe: relax any bound in Create and the matching case below stops refusing. An absent file is a
	// distinct, retryable refusal rather than a failure, because a caller waits for a writer that starts later.
	TEST_F(WindowsFileTailReaderTests, RejectsInvalidPathsAndOptions)
	{
		const std::filesystem::path file = root / "bounds.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(file));
			ASSERT_TRUE(writer.Append("x\n"));
		}

		rox::core::Result<rox::os::WindowsFileTailReader> emptyPath = os::WindowsFileTailReader::Create(resource, core::StringView());
		ASSERT_TRUE(!emptyPath);
		ASSERT_TRUE(emptyPath.GetError().DiagnosticID == "ROX-PLATFORM-TAIL-PATH");

		os::WindowsFileTailReaderOptions zeroRead;
		zeroRead.MaximumBytesPerRead = 0;
		rox::core::Result<rox::os::WindowsFileTailReader> zero = os::WindowsFileTailReader::Create(resource, PathText(file), zeroRead);
		ASSERT_TRUE(!zero);
		ASSERT_TRUE(zero.GetError().DiagnosticID == "ROX-PLATFORM-TAIL-OPTIONS");

		os::WindowsFileTailReaderOptions hugeRead;
		hugeRead.MaximumBytesPerRead = os::MAXIMUM_FILE_TAIL_READ_BYTES + 1;
		rox::core::Result<rox::os::WindowsFileTailReader> huge = os::WindowsFileTailReader::Create(resource, PathText(file), hugeRead);
		ASSERT_TRUE(!huge);
		ASSERT_TRUE(huge.GetError().DiagnosticID == "ROX-PLATFORM-TAIL-OPTIONS");

		rox::core::Result<rox::os::WindowsFileTailReader> missing =
			os::WindowsFileTailReader::Create(resource, PathText(root / "no-such-file.jsonl"));
		ASSERT_TRUE(!missing);
		ASSERT_TRUE(missing.GetError().Code == core::EErrorCode::NotFound);
		ASSERT_TRUE(missing.GetError().DiagnosticID == "ROX-PLATFORM-TAIL-MISSING");
	}

	// Probe: drop FILE_SHARE_WRITE from State::TryOpen and this goes red at the writer's own open, which is
	// the whole reason the reader exists: the sink must keep writing while the Editor reads.
	TEST_F(WindowsFileTailReaderTests, AllowsConcurrentAppendWriter)
	{
		const std::filesystem::path file = root / "shared.jsonl";
		{
			AppendingWriter seed;
			ASSERT_TRUE(seed.Open(file));
			ASSERT_TRUE(seed.Append("seed\n"));
		}

		rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(file));
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();
		AppendingWriter writer;
		ASSERT_TRUE(writer.Open(file));
		ASSERT_TRUE(writer.Append("live\n"));
		const HarvestResult batch = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!batch.bFailed);
		ASSERT_TRUE(batch.Text == "seed\nlive\n");
	}

	// Probe: delete the move assignments' DestroyObject call and the tracking resource below reports a live
	// allocation, because the moved-into reader leaks the state it was holding.
	TEST_F(WindowsFileTailReaderTests, TransfersReaderOwnership)
	{
		const std::filesystem::path file = root / "moved.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(file));
			ASSERT_TRUE(writer.Append("one\n"));
		}

		rox::core::Result<rox::os::WindowsFileTailReader> firstCreated = os::WindowsFileTailReader::Create(resource, PathText(file));
		ASSERT_TRUE(firstCreated);
		os::WindowsFileTailReader first = rox::core::Move(firstCreated).TakeValue();
		rox::core::Result<rox::os::WindowsFileTailReader> secondCreated = os::WindowsFileTailReader::Create(resource, PathText(file));
		ASSERT_TRUE(secondCreated);
		os::WindowsFileTailReader second = rox::core::Move(secondCreated).TakeValue();
		second = rox::core::Move(first);
		ASSERT_TRUE(second.IsValid());
		ASSERT_TRUE(!first.IsValid());
		const HarvestResult batch = harvestUntilBytes(second, true);
		ASSERT_TRUE(!batch.bFailed);
		ASSERT_TRUE(batch.Text == "one\n");
	}

	// A pending read may return no bytes; subsequent reads must deliver the remaining data exactly once.
	// Cached local files do not deterministically exercise delayed overlapped completion.
	TEST_F(WindowsFileTailReaderTests, ReturnsEmptyWhenNoReadIsReady)
	{
		const std::filesystem::path file = root / "nonblocking.jsonl";
		AppendingWriter writer;
		ASSERT_TRUE(writer.Open(file));
		// A megabyte in one append, so the read the reader arms is the largest one it can make.
		const std::string payload(1'048'576, 'n');
		ASSERT_TRUE(writer.Append(payload));

		os::WindowsFileTailReaderOptions options;
		options.MaximumBytesPerRead = os::MAXIMUM_FILE_TAIL_READ_BYTES;
		options.BackfillBytes = os::MAXIMUM_FILE_TAIL_READ_BYTES;
		rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(file), options);
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();

		const HarvestResult first = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!first.bFailed);
		ASSERT_TRUE(first.Text.size() == payload.size());
		ASSERT_TRUE(first.Text == payload);
		// An idle reader answers immediately with an empty view rather than failing or waiting.
		const HarvestResult idle = harvestUntilBytes(reader, false);
		ASSERT_TRUE(!idle.bFailed);
		ASSERT_TRUE(idle.Text.empty());
	}

	// An earlier read view must remain valid until the next Read begins.
	TEST_F(WindowsFileTailReaderTests, DestructionRetiresPendingRead)
	{
		const std::filesystem::path file = root / "churn.jsonl";
		AppendingWriter writer;
		ASSERT_TRUE(writer.Open(file));

		for (int round = 0; round < 32; ++round)
		{
			ASSERT_TRUE(writer.Append(std::string(64'000, 'k')));
			os::WindowsFileTailReaderOptions options;
			options.MaximumBytesPerRead = os::MAXIMUM_FILE_TAIL_READ_BYTES;
			options.BackfillBytes = os::MAXIMUM_FILE_TAIL_READ_BYTES;
			rox::core::Result<rox::os::WindowsFileTailReader> created =
				os::WindowsFileTailReader::Create(resource, PathText(file), options);
			ASSERT_TRUE(created);
			os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();
			// One Read arms a large read and may or may not have harvested it; the destructor that runs
			// at the end of this iteration is the case under test either way.
			rox::core::Result<rox::core::Span<const rox::core::Byte>> batch = reader.Read();
			ASSERT_TRUE(batch);
		}

	}

	// Truncation below the consumed offset is terminal; later writes must not resume the old tail.
	// Truncation that regrows beyond the offset between polls is not observable from file size alone.
	TEST_F(WindowsFileTailReaderTests, DetectsTruncationEvenAfterRegrowth)
	{
		const std::filesystem::path file = root / "regrow.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(file));
			ASSERT_TRUE(writer.Append(std::string(4'096, 'a')));
		}
		os::WindowsFileTailReaderOptions options;
		options.BackfillBytes = 0;
		rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(file), options);
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();
		// BackfillBytes is zero, so the reader begins at the end of a file that already has content:
		// that is mid-file by definition, and the first read has nothing to report.
		ASSERT_TRUE(reader.BeganMidFile());
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(file));
			ASSERT_TRUE(writer.Append(std::string(64, 'b')));
		}

		const HarvestResult appended = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!appended.bFailed);
		ASSERT_TRUE(appended.Text.size() == 64);

		// Truncate to nothing and regrow to less than the offset the reader had reached. A reader that
		// only compared the current length against zero would resume inside foreign bytes.
		{
			const HANDLE truncated = CreateFileW(file.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
				TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			ASSERT_TRUE(truncated != INVALID_HANDLE_VALUE);
			CloseHandle(truncated);
		}
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(file));
			ASSERT_TRUE(writer.Append(std::string(1'024, 'c')));
		}

		const HarvestResult regrown = harvestUntilBytes(reader, false);
		ASSERT_TRUE(regrown.bFailed);
		ASSERT_TRUE(regrown.DiagnosticID == "ROX-PLATFORM-TAIL-GONE");
	}

	// Querying identity inside Read would fail the assertion that an unlinked file remains readable before a probe.
	TEST_F(WindowsFileTailReaderTests, DetectsReplacementAndKeepsTerminalIdentity)
	{
		const std::filesystem::path file = root / "replaced.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(file));
			ASSERT_TRUE(writer.Append("first generation\n"));
		}

		rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(file));
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();
		// Nothing has been asked yet, and that is its own answer.
		ASSERT_TRUE(reader.ReadIdentity() == os::EFileTailIdentity::Unknown);
		const HarvestResult firstGeneration = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!firstGeneration.bFailed);
		ASSERT_TRUE(firstGeneration.Text == "first generation\n");
		ASSERT_TRUE(!reader.IsReadPending());
		ASSERT_TRUE(askIdentity(reader) == os::EFileTailIdentity::Same);

		// The file this reader holds is deleted. The handle keeps it alive and readable, and the deletion
		// is visible ON that handle, so the answer does not depend on what the path now names or on
		// whether the path could be opened at all.
		std::error_code fileSystemError;
		ASSERT_TRUE(std::filesystem::remove(file, fileSystemError));
		ASSERT_TRUE(!fileSystemError);
		ASSERT_TRUE(std::filesystem::create_directory(file, fileSystemError));
		ASSERT_TRUE(!fileSystemError);

		// Read still succeeds against the unlinked file while nothing has been asked, because Read never
		// touches the path. Identity changes take effect only after an explicit probe.
		const HarvestResult readAfterDeletion = harvestUntilBytes(reader, false);
		ASSERT_TRUE(!readAfterDeletion.bFailed);

		ASSERT_TRUE(askIdentity(reader) == os::EFileTailIdentity::Replaced);
		reader.ApplyPublishedIdentity();
		const HarvestResult afterReplacement = harvestUntilBytes(reader, false);
		ASSERT_TRUE(afterReplacement.bFailed);
		ASSERT_TRUE(afterReplacement.DiagnosticID == "ROX-PLATFORM-TAIL-REPLACED");
		ASSERT_TRUE(reader.ReadIdentity() == os::EFileTailIdentity::Replaced);
		// And a terminal reader answers a further request without starting anything.
		reader.RequestIdentityProbe();
		ASSERT_TRUE(reader.ReadIdentity() == os::EFileTailIdentity::Replaced);
	}

	// Deletion and rename require separate checks: an unlinked handle may retain its name, while a renamed file remains alive.
	// Native metadata-query failures are not deterministically exercised here.
	TEST_F(WindowsFileTailReaderTests, IdentityProbeDetectsRenameAndDeletion)
	{
		const std::filesystem::path directory = root / "handle-identity";
		std::filesystem::create_directories(directory);

		// Half one: a rename. The file is alive and undeleted; only its NAME moved.
		const std::filesystem::path renamedPath = directory / "renamed.jsonl";
		const std::filesystem::path movedPath = directory / "renamed.moved.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(renamedPath));
			ASSERT_TRUE(writer.Append("alive\n"));
		}

		rox::core::Result<rox::os::WindowsFileTailReader> renamedCreated =
			os::WindowsFileTailReader::Create(resource, PathText(renamedPath));
		ASSERT_TRUE(renamedCreated);
		os::WindowsFileTailReader renamed = rox::core::Move(renamedCreated).TakeValue();
		ASSERT_TRUE(askIdentity(renamed) == os::EFileTailIdentity::Same);
		std::error_code renameError;
		std::filesystem::rename(renamedPath, movedPath, renameError);
		ASSERT_TRUE(!renameError);
		ASSERT_TRUE(askIdentity(renamed) == os::EFileTailIdentity::Replaced);

		// Half two: a deletion. The name is gone and the file lives only behind this handle.
		const std::filesystem::path deletedPath = directory / "deleted.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(deletedPath));
			ASSERT_TRUE(writer.Append("doomed\n"));
		}

		rox::core::Result<rox::os::WindowsFileTailReader> deletedCreated =
			os::WindowsFileTailReader::Create(resource, PathText(deletedPath));
		ASSERT_TRUE(deletedCreated);
		os::WindowsFileTailReader deleted = rox::core::Move(deletedCreated).TakeValue();
		ASSERT_TRUE(askIdentity(deleted) == os::EFileTailIdentity::Same);
		std::error_code removeError;
		ASSERT_TRUE(std::filesystem::remove(deletedPath, removeError));
		ASSERT_TRUE(!removeError);
		ASSERT_TRUE(askIdentity(deleted) == os::EFileTailIdentity::Replaced);
	}

	// Deferring identity publication would fail the immediate Same assertion; probing must not allocate.
	TEST_F(WindowsFileTailReaderTests, IdentityProbeDoesNotAllocate)
	{
		const std::filesystem::path file = root / "identity-cost.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(file));
			ASSERT_TRUE(writer.Append("cost\n"));
		}
		CountingMemoryResource counting(core::SystemMemoryResource::Get());
		{
			rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(counting, PathText(file));
			ASSERT_TRUE(created);
			os::WindowsFileTailReader counted = rox::core::Move(created).TakeValue();
			const rox::core::usize afterCreate = counting.GetAllocationCallCount();

			for (int round = 0; round < 64; ++round)
			{
				ASSERT_TRUE(askIdentity(counted) == os::EFileTailIdentity::Same);
			}
			ASSERT_TRUE(counting.GetAllocationCallCount() == afterCreate);
		}

	}

	// Renaming preserves a path that can be opened exclusively to verify handle retirement.
	// The handle must remain open until ApplyPublishedIdentity, even after a probe reports Replaced.
	TEST_F(WindowsFileTailReaderTests, ReplacementTransitionReleasesOldHandle)
	{
		const std::filesystem::path directory = root / "handle-lifetime";
		std::filesystem::create_directories(directory);
		const std::filesystem::path live = directory / "live.jsonl";
		const std::filesystem::path moved = directory / "live.moved.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(live));
			ASSERT_TRUE(writer.Append("held\n"));
		}

		rox::core::Result<rox::os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(live));
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();
		const HarvestResult drained = harvestUntilBytes(reader, true);
		ASSERT_TRUE(!drained.bFailed && drained.Text == "held\n");

		const auto canOpenExclusively = [](const std::filesystem::path& path)
			{
				const HANDLE exclusive =
					CreateFileW(path.wstring().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

				if (exclusive == INVALID_HANDLE_VALUE)
				{
					return false;
				}
				CloseHandle(exclusive);
				return true;
			};

		// The reader holds it, so nobody else can take it exclusively. Without this the assertion after
		// the transition would pass against a reader that never opened anything at all.
		ASSERT_TRUE(!canOpenExclusively(live));

		std::error_code renameError;
		std::filesystem::rename(live, moved, renameError);
		ASSERT_TRUE(!renameError);
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(live));
			ASSERT_TRUE(writer.Append("replacement\n"));
		}
		ASSERT_TRUE(!canOpenExclusively(moved));

		reader.RequestIdentityProbe();
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);

		while (reader.ReadIdentity() != os::EFileTailIdentity::Replaced && rox::core::MonotonicClock::now() < deadline)
		{
			rox::core::SleepFor(rox::core::Milliseconds(1));
		}
		ASSERT_TRUE(reader.ReadIdentity() == os::EFileTailIdentity::Replaced);
		// Published but not applied: nothing has been released, because the worker only answers.
		ASSERT_TRUE(!canOpenExclusively(moved));

		reader.ApplyPublishedIdentity();
		// Released AT the transition, with the reader object still alive, still in scope and still
		// answering -- which is what separates "released when the replacement was seen" from "released
		// when the reader was destroyed".
		ASSERT_TRUE(canOpenExclusively(moved));
		ASSERT_TRUE(reader.IsValid());
		ASSERT_TRUE(reader.ReadIdentity() == os::EFileTailIdentity::Replaced);
		const HarvestResult afterTransition = harvestUntilBytes(reader, false);
		ASSERT_TRUE(afterTransition.bFailed);
		ASSERT_TRUE(afterTransition.DiagnosticID == "ROX-PLATFORM-TAIL-REPLACED");
	}

	TEST_F(WindowsFileTailReaderTests, MissingFileRefusalAvoidsAllocation)
	{
		// The whole refusal, not only the part that reaches the injected resource: TryCreate returns a
		// value and never constructs a core::Error, whose three owned strings would otherwise be built --
		// out of the global resource, where the counting resource below could not see them -- on every
		// polling round for the whole session.
		CountingMemoryResource absent(core::SystemMemoryResource::Get());
		os::WindowsFileTailReader reader;

		for (int round = 0; round < 64; ++round)
		{
			const os::EFileTailCreateResult created =
				os::WindowsFileTailReader::TryCreate(absent, PathText(root / "never-created.jsonl"), {}, &reader);
			ASSERT_TRUE(created == os::EFileTailCreateResult::Missing);
			ASSERT_TRUE(!reader.IsValid());
			ASSERT_TRUE(os::GetFileTailCreateDiagnosticID(created) == "ROX-PLATFORM-TAIL-MISSING");
		}
		ASSERT_TRUE(absent.GetAllocationCallCount() == 0);

		// The bound refusals and an unconvertible path cost nothing either, and each keeps its identity.
		os::WindowsFileTailReaderOptions bad;
		bad.MaximumBytesPerRead = 0;
		ASSERT_TRUE(os::WindowsFileTailReader::TryCreate(absent, PathText(root / "never-created.jsonl"), bad, &reader) ==
			os::EFileTailCreateResult::InvalidOptions);
		ASSERT_TRUE(os::WindowsFileTailReader::TryCreate(absent, core::StringView(), {}, &reader) == os::EFileTailCreateResult::InvalidPath);
		ASSERT_TRUE(absent.GetAllocationCallCount() == 0);

		// Create may allocate diagnostics on refusal; TryCreate must leave the injected resource untouched.
		CountingMemoryResource wrapper(core::SystemMemoryResource::Get());
		rox::core::Result<rox::os::WindowsFileTailReader> refused =
			os::WindowsFileTailReader::Create(wrapper, PathText(root / "never-created.jsonl"));
		ASSERT_TRUE(!refused);
		ASSERT_TRUE(refused.GetError().DiagnosticID == "ROX-PLATFORM-TAIL-MISSING");
		ASSERT_TRUE(wrapper.GetAllocationCallCount() != 0);
		// The three owned strings of one error, and nothing else: a wrapper that allocated per call in
		// some other way would show a different count.
		const rox::core::usize afterFirst = wrapper.GetAllocationCallCount();
		rox::core::Result<rox::os::WindowsFileTailReader> refusedAgain =
			os::WindowsFileTailReader::Create(wrapper, PathText(root / "never-created.jsonl"));
		ASSERT_TRUE(!refusedAgain);
		ASSERT_TRUE(wrapper.GetAllocationCallCount() == afterFirst * 2);
	}

	// Backfill follows the initial file boundary, not the number of Read calls or the first nonempty chunk.
	TEST_F(WindowsFileTailReaderTests, TracksBackfillUntilDelivered)
	{
		// A file with content when the reader attaches owes a backfill.
		const std::filesystem::path seededPath = root / "backfill-owed.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(seededPath));
			ASSERT_TRUE(writer.Append("already here\n"));
		}

		rox::core::Result<rox::os::WindowsFileTailReader> seededCreated = os::WindowsFileTailReader::Create(resource, PathText(seededPath));
		ASSERT_TRUE(seededCreated);
		os::WindowsFileTailReader seeded = rox::core::Move(seededCreated).TakeValue();
		ASSERT_TRUE(seeded.HasPendingBackfill());
		// Reaching the attach-time boundary discharges it, and nothing else does.
		const HarvestResult delivered = harvestUntilBytes(seeded, true);
		ASSERT_TRUE(!delivered.bFailed && delivered.Text == "already here\n");
		ASSERT_TRUE(!seeded.HasPendingBackfill());
		// It stays discharged across an idle round.
		rox::core::Result<rox::core::Span<const rox::core::Byte>> idle = seeded.Read();
		ASSERT_TRUE(idle && idle.Value().IsEmpty());
		ASSERT_TRUE(!seeded.HasPendingBackfill());

		// A multi-read backfill remains pending until its captured end offset is reached.
		const std::filesystem::path multiPath = root / "backfill-multi.jsonl";
		{
			AppendingWriter writer;
			ASSERT_TRUE(writer.Open(multiPath));
			ASSERT_TRUE(writer.Append(std::string(200, 'm')));
		}
		os::WindowsFileTailReaderOptions chunked;
		chunked.MaximumBytesPerRead = 64;
		chunked.BackfillBytes = os::MAXIMUM_FILE_TAIL_READ_BYTES;
		rox::core::Result<rox::os::WindowsFileTailReader> multiCreated =
			os::WindowsFileTailReader::Create(resource, PathText(multiPath), chunked);
		ASSERT_TRUE(multiCreated);
		os::WindowsFileTailReader multi = rox::core::Move(multiCreated).TakeValue();
		ASSERT_TRUE(multi.HasPendingBackfill());
		rox::core::usize drained = 0;

		for (int round = 0; round < 8 && multi.HasPendingBackfill(); ++round)
		{
			const HarvestResult chunk = harvestUntilBytes(multi, true);
			ASSERT_TRUE(!chunk.bFailed);
			drained += chunk.Text.size();
			// Every round before the last still owes bytes; the last one does not.
			ASSERT_TRUE(multi.HasPendingBackfill() == (drained < 200));
		}
		ASSERT_TRUE(drained == 200);
		ASSERT_TRUE(!multi.HasPendingBackfill());

		// A file that is empty when the reader attaches owes nothing, so the first bytes it ever sees are
		// a live append and not history.
		const std::filesystem::path emptyPath = root / "backfill-none.jsonl";
		AppendingWriter writer;
		ASSERT_TRUE(writer.Open(emptyPath));
		rox::core::Result<rox::os::WindowsFileTailReader> emptyCreated = os::WindowsFileTailReader::Create(resource, PathText(emptyPath));
		ASSERT_TRUE(emptyCreated);
		os::WindowsFileTailReader empty = rox::core::Move(emptyCreated).TakeValue();
		ASSERT_TRUE(!empty.HasPendingBackfill());
		ASSERT_TRUE(!empty.IsReadPending());
		ASSERT_TRUE(writer.Append("live\n"));
		const HarvestResult live = harvestUntilBytes(empty, true);
		ASSERT_TRUE(!live.bFailed && live.Text == "live\n");
		ASSERT_TRUE(!empty.HasPendingBackfill());
	}

	TEST_F(WindowsFileTailReaderTests, AllocationFailurePreservesReaderAndClosesFile)
	{
		const std::filesystem::path originalPath = root / "original.txt";
		const std::filesystem::path refusedPath = root / "refused.txt";
		{
			AppendingWriter original;
			AppendingWriter refused;
			ASSERT_TRUE(original.Open(originalPath));
			ASSERT_TRUE(original.Append("original\n"));
			ASSERT_TRUE(refused.Open(refusedPath));
		}

		core::Result<os::WindowsFileTailReader> created = os::WindowsFileTailReader::Create(resource, PathText(originalPath));
		ASSERT_TRUE(created);
		os::WindowsFileTailReader reader = rox::core::Move(created).TakeValue();

		for (core::usize attempts = 0; attempts < 2; ++attempts)
		{
			core::FailingMemoryResource failing(resource, attempts);
			const os::EFileTailCreateResult result = os::WindowsFileTailReader::TryCreate(failing, PathText(refusedPath), {}, &reader);
			EXPECT_EQ(result, os::EFileTailCreateResult::Allocation);
			EXPECT_EQ(os::GetFileTailCreateDiagnosticID(result), "ROX-PLATFORM-TAIL-ALLOCATION");
			EXPECT_TRUE(reader.IsValid());
			HANDLE exclusive = CreateFileW(refusedPath.wstring().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			ASSERT_NE(exclusive, INVALID_HANDLE_VALUE);
			EXPECT_TRUE(CloseHandle(exclusive));
		}

		const HarvestResult original = harvestUntilBytes(reader, true);
		EXPECT_FALSE(original.bFailed);
		EXPECT_EQ(original.Text, "original\n");
	}

} // namespace
