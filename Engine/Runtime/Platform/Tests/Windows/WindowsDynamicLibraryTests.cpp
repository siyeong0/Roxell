#include <WindowsDynamicLibrary.h>
#include <Utility.hpp>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <string>

using namespace rox::core;
using namespace rox::os;

namespace
{
	std::string systemLibraryPath()
	{
		char directory[MAX_PATH]{};
		const UINT length = GetSystemDirectoryA(directory, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
		{
			return {};
		}
		return std::string(directory, length) + "\\kernel32.dll";
	}
}

TEST(WindowsDynamicLibraryTests, InvalidPathsReportErrorWithoutAllocations)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	for (const StringView path : { StringView(), StringView("relative.dll"), StringView("C:relative.dll"), StringView("\\rooted.dll"), StringView("//server"), StringView("//server/share"), StringView("C:/bad\xff.dll"), StringView("C:/a\0b.dll", 10) })
	{
		const Result<DynamicLibraryOwner> result = LoadWindowsDynamicLibrary(tracking, path);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, EErrorCode::InvalidArgument);
		EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-LIBRARY-PATH");
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
}

TEST(WindowsDynamicLibraryTests, LoadFindMoveAndCloseTransferOwnership)
{
	const std::string path = systemLibraryPath();
	ASSERT_FALSE(path.empty());
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		Result<DynamicLibraryOwner> result = LoadWindowsDynamicLibrary(tracking, { path.data(), path.size() });
		ASSERT_TRUE(result);
		DynamicLibraryOwner owner = rox::core::Move(result).TakeValue();
		EXPECT_TRUE(owner.FindSymbol("GetCurrentProcessId"));
		const Result<DynamicLibrarySymbol> missing = owner.FindSymbol("RoxellMissingExport");
		ASSERT_FALSE(missing);
		EXPECT_EQ(missing.GetError().Code, EErrorCode::NotFound);
		EXPECT_EQ(missing.GetError().DiagnosticID, "ROX-PLATFORM-LIBRARY-EXPORT");
		DynamicLibraryOwner moved(rox::core::Move(owner));
		EXPECT_FALSE(owner);
		EXPECT_TRUE(moved.FindSymbol("GetCurrentProcessId"));
		EXPECT_TRUE(moved.Close());
		EXPECT_FALSE(moved);
		EXPECT_TRUE(moved.Close());
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
}

TEST(WindowsDynamicLibraryTests, AllocationFailureDoesNotLeak)
{
	const std::string path = systemLibraryPath();
	ASSERT_FALSE(path.empty());
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	for (usize failAt = 0; failAt < 2; ++failAt)
	{
		FailingMemoryResource failing(tracking, failAt);
		const Result<DynamicLibraryOwner> result = LoadWindowsDynamicLibrary(failing, { path.data(), path.size() });
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-LIBRARY-ALLOCATION");
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	}
}

TEST(WindowsDynamicLibraryTests, ClosedOwnerRejectsSymbolLookup)
{
	const std::string path = systemLibraryPath();
	ASSERT_FALSE(path.empty());
	Result<DynamicLibraryOwner> loaded = LoadWindowsDynamicLibrary(SystemMemoryResource::Get(), { path.data(), path.size() });
	ASSERT_TRUE(loaded);
	DynamicLibraryOwner owner = rox::core::Move(loaded).TakeValue();
	ASSERT_TRUE(owner.Close());
	ASSERT_DEATH((void)owner.FindSymbol("GetCurrentProcessId"), "Symbol lookup requires an open dynamic library owner");
}

TEST(WindowsDynamicLibraryTests, MissingImageReportsLoadFailureWithoutLeaking)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	const Result<DynamicLibraryOwner> result = LoadWindowsDynamicLibrary(tracking, "C:/roxell-dll-does-not-exist.dll");
	ASSERT_FALSE(result);
	EXPECT_EQ(result.GetError().Code, EErrorCode::ExternalFailure);
	EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-LIBRARY-LOAD");
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}
