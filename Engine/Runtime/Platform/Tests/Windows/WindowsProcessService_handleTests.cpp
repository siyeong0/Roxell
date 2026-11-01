#include <WindowsProcessService.h>
#include <TypeVocabulary.hpp>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

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
			if (GetFileInformationByHandle(entry.Value, &file) && file.dwVolumeSerialNumber == identity.dwVolumeSerialNumber &&
				file.nFileIndexHigh == identity.nFileIndexHigh && file.nFileIndexLow == identity.nFileIndexLow)
			{
				++count;
			}
		}
		return count;
	}

	TEST(WindowsProcessImageHandleTests, SuccessfulAndRefusedHashesCloseExecutableFileHandles)
	{
		wchar_t executable[32768] = {};
		const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(rox::core::Extent<decltype(executable)>));
		ASSERT_GT(length, 0u);
		ASSERT_LT(length, rox::core::Extent<decltype(executable)>);
		const std::vector<HandleEntry> before = getHandles();
		ASSERT_FALSE(before.empty());
		HANDLE sample = CreateFileW(executable, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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
		ULONG fileType = 0;
		for (const HandleEntry& entry : during)
		{
			if (entry.Value == sample)
			{
				fileType = entry.Type;
				break;
			}
		}
		ASSERT_NE(fileType, 0u);
		const core::usize beforeCount = countFileHandles(before, fileType, identity);
		EXPECT_EQ(countFileHandles(during, fileType, identity), beforeCount + 1);
		ASSERT_TRUE(CloseHandle(sample));
		sampleGuard.Value = INVALID_HANDLE_VALUE;
		for (core::uint32 index = 0; index < 3; ++index)
		{
			const core::Result<core::Hash256> small = os::ReadCurrentWindowsProcessImageHash(1);
			ASSERT_FALSE(small);
			EXPECT_EQ(small.GetError().Code, core::EErrorCode::Unsupported);
			EXPECT_EQ(small.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-IMAGE-LIMIT");
			EXPECT_TRUE(os::ReadCurrentWindowsProcessImageHash());
		}
		const std::vector<HandleEntry> after = getHandles();
		ASSERT_FALSE(after.empty());
		EXPECT_EQ(countFileHandles(after, fileType, identity), beforeCount);
	}
} // namespace
