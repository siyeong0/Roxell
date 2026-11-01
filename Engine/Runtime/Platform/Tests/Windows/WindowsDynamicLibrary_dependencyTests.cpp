#include <WindowsDynamicLibrary.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <filesystem>
#include <string>

using namespace rox::core;
using namespace rox::os;

namespace
{
	std::string utf8Path(const std::filesystem::path& path)
	{
		const std::u8string text = path.u8string();
		return { reinterpret_cast<const char*>(text.data()), text.size() };
	}

	class WindowsDynamicLibraryDependencyTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			wchar_t temporaryPath[MAX_PATH]{};
			ASSERT_NE(GetTempPathW(MAX_PATH, temporaryPath), 0u);
			wchar_t uniquePath[MAX_PATH]{};
			ASSERT_NE(GetTempFileNameW(temporaryPath, L"rox", 0, uniquePath), 0u);
			ASSERT_TRUE(DeleteFileW(uniquePath));
			m_Directory = uniquePath;
			ASSERT_TRUE(std::filesystem::create_directory(m_Directory));
		}

		void TearDown() override
		{
			std::error_code error;
			std::filesystem::remove_all(m_Directory, error);
			EXPECT_FALSE(error);
			EXPECT_EQ(m_Tracking.GetStatistics().LiveAllocationCount, 0u);
		}

	protected:
		std::filesystem::path m_Directory;
		TrackingMemoryResource m_Tracking{ SystemMemoryResource::Get() };
	};
}

TEST_F(WindowsDynamicLibraryDependencyTests, UnicodePathLoadsTheSelectedImageAndUnloadsIt)
{
	const std::filesystem::path directory = m_Directory / L"\ubaa8\ub4c8";
	ASSERT_TRUE(std::filesystem::create_directory(directory));
	const std::filesystem::path image = directory / L"\uc21c\uc218.dll";
	ASSERT_TRUE(std::filesystem::copy_file(ROXELL_TEST_MODULE_C, image));
	const std::string path = utf8Path(image);
	{
		Result<DynamicLibraryOwner> loaded = LoadWindowsDynamicLibrary(m_Tracking, { path.data(), path.size() });
		ASSERT_TRUE(loaded);
		Result<DynamicLibrarySymbol> symbol = loaded.Value().FindSymbol("RoxellGameModule_GetApi");
		ASSERT_TRUE(symbol);
		HMODULE module = nullptr;
		ASSERT_TRUE(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(symbol.Value()), &module));
		wchar_t actual[32768]{};
		ASSERT_NE(GetModuleFileNameW(module, actual, 32768), 0u);
		EXPECT_TRUE(std::filesystem::equivalent(actual, image));
		EXPECT_TRUE(loaded.Value().Close());
	}
	EXPECT_TRUE(std::filesystem::remove(image));
}

TEST_F(WindowsDynamicLibraryDependencyTests, DependencyMustBeInTheExplicitImageDirectory)
{
	const std::filesystem::path dependent = ROXELL_TEST_MODULE_DEPENDENT;
	const std::filesystem::path dependency = ROXELL_TEST_MODULE_CPP;
	const std::filesystem::path image = m_Directory / dependent.filename();
	ASSERT_TRUE(std::filesystem::copy_file(dependent, image));
	const std::string path = utf8Path(image);
	const Result<DynamicLibraryOwner> missing = LoadWindowsDynamicLibrary(m_Tracking, { path.data(), path.size() });
	ASSERT_FALSE(missing);
	EXPECT_EQ(missing.GetError().Code, EErrorCode::ExternalFailure);
	EXPECT_EQ(missing.GetError().DiagnosticID, "ROX-PLATFORM-LIBRARY-LOAD");
	EXPECT_EQ(m_Tracking.GetStatistics().LiveAllocationCount, 0u);
	ASSERT_TRUE(std::filesystem::copy_file(dependency, m_Directory / dependency.filename()));
	Result<DynamicLibraryOwner> loaded = LoadWindowsDynamicLibrary(m_Tracking, { path.data(), path.size() });
	ASSERT_TRUE(loaded);
	EXPECT_TRUE(loaded.Value().Close());
}
