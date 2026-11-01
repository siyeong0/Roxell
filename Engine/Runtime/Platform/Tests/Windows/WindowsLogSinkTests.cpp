#include <WindowsLogSink.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	std::string utf8Path(const std::filesystem::path& path)
	{
		const std::u8string bytes = path.u8string();
		return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
	}

	std::string readFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	}

	class WindowsLogSinkTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			std::filesystem::path root = std::filesystem::current_path();
			while (!root.empty() && !std::filesystem::exists(root / "AGENTS.md"))
			{
				const std::filesystem::path parent = root.parent_path();
				if (parent == root)
				{
					break;
				}
				root = parent;
			}
			ASSERT_TRUE(std::filesystem::exists(root / "AGENTS.md"));
			m_Root = root / ".cache" / ("platform-log-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
			ASSERT_TRUE(std::filesystem::create_directories(m_Root));
		}

		void TearDown() override
		{
			std::error_code error;
			std::filesystem::remove_all(m_Root, error);
			EXPECT_FALSE(error) << error.message();
		}

	protected:
		std::filesystem::path m_Root;
	};

	TEST_F(WindowsLogSinkTests, WritesAndReopensInAppendMode)
	{
		const std::filesystem::path path = m_Root / "records.jsonl";
		const std::string name = utf8Path(path);
		core::LogEvent event;
		{
			os::WindowsLogSink sink;
			ASSERT_TRUE(sink.TryOpen(name));
			EXPECT_TRUE(sink.IsOpen());
			sink.Write(event, "{\"record\":1}\n");
			sink.Flush();
			EXPECT_FALSE(sink.HasWriteFailure());
			EXPECT_EQ(readFile(path), "{\"record\":1}\n");
			sink.Close();
			EXPECT_FALSE(sink.IsOpen());
			ASSERT_TRUE(sink.TryOpen(name));
			sink.Write(event, "{\"record\":2}\n");
		}
		EXPECT_EQ(readFile(path), "{\"record\":1}\n{\"record\":2}\n");
	}

	TEST_F(WindowsLogSinkTests, CreatesParentsAndRejectsInvalidPathsWithoutReplacingOpenFile)
	{
		const std::filesystem::path path = m_Root / "nested" / "log.jsonl";
		const std::string name = utf8Path(path);
		os::WindowsLogSink sink;
		EXPECT_FALSE(sink.TryOpenCreatingDirectories(core::StringView("bad\0path", 8)));
		EXPECT_FALSE(sink.IsOpen());
		ASSERT_TRUE(sink.TryOpenCreatingDirectories(name));
		EXPECT_FALSE(sink.TryOpenCreatingDirectories(utf8Path(m_Root / "other.jsonl")));
		core::LogEvent event;
		sink.Write(event, "{\"kept\":true}\n");
		sink.Close();
		EXPECT_FALSE(sink.HasWriteFailure());
		EXPECT_EQ(readFile(path), "{\"kept\":true}\n");
		EXPECT_FALSE(std::filesystem::exists(m_Root / "other.jsonl"));
	}

	TEST_F(WindowsLogSinkTests, ExecutableRelativePathCreatesParentsAndWrites)
	{
		wchar_t executable[32768]{};
		const DWORD size = GetModuleFileNameW(nullptr, executable, 32768);
		ASSERT_GT(size, 0);
		ASSERT_LT(size, 32768);
		const std::filesystem::path path = m_Root / "relative" / "log.jsonl";
		const std::filesystem::path executableDirectory = std::filesystem::path(executable).parent_path();
		const std::string relative = utf8Path(std::filesystem::relative(path, executableDirectory));
		os::WindowsLogSink sink;
		ASSERT_TRUE(sink.TryOpenRelativeToExecutable(relative));
		core::LogEvent event;
		event.Record.Level = core::ELogLevel::Error;
		sink.Write(event, "{\"relative\":true}\n");
		sink.Close();
		EXPECT_FALSE(sink.HasWriteFailure());
		EXPECT_EQ(readFile(path), "{\"relative\":true}\n");
	}
	TEST_F(WindowsLogSinkTests, DirectoryCreationRejectsReparsePointWithoutWritingTarget)
	{
		const std::filesystem::path target = m_Root / "target";
		ASSERT_TRUE(std::filesystem::create_directory(target));
		const std::filesystem::path link = m_Root / "link";
		if (!CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2))
		{
			GTEST_SKIP() << "Creating a directory symbolic link is unavailable: " << GetLastError();
		}
		os::WindowsLogSink sink;
		EXPECT_FALSE(sink.TryOpenCreatingDirectories(utf8Path(link / "log.jsonl")));
		EXPECT_FALSE(sink.IsOpen());
		EXPECT_FALSE(std::filesystem::exists(target / "log.jsonl"));
	}
}
