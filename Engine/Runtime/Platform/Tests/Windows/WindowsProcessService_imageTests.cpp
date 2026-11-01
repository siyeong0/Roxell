#include <WindowsProcessService.h>
#include <Types.h>
#include <TypeVocabulary.hpp>
#include <Sha256.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	TEST(WindowsProcessImageTests, HashMatchesExecutableBytesReadIndependently)
	{
		wchar_t executable[32768] = {};
		const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(rox::core::Extent<decltype(executable)>));
		ASSERT_GT(length, 0u);
		ASSERT_LT(length, rox::core::Extent<decltype(executable)>);
		std::ifstream input(std::filesystem::path(executable), std::ios::binary | std::ios::ate);
		ASSERT_TRUE(input.good());
		const std::streampos size = input.tellg();
		ASSERT_GT(size, 0);
		ASSERT_LE(size, static_cast<std::streamoff>(os::MAXIMUM_PROCESS_IMAGE_HASH_BYTES));
		std::vector<core::Byte> bytes(static_cast<rox::core::usize>(size));
		input.seekg(0);
		ASSERT_TRUE(input.read(reinterpret_cast<char*>(bytes.data()), size));
		const core::Hash256 expected = core::ComputeSha256(bytes);
		const core::Result<core::Hash256> actual = os::ReadCurrentWindowsProcessImageHash();
		ASSERT_TRUE(actual);
		EXPECT_EQ(actual.Value(), expected);
	}

	TEST(WindowsProcessImageTests, RejectsLimitsOutsideContractAndCanRetry)
	{
		for (const core::uint64 limit : { 0ull, os::MAXIMUM_PROCESS_IMAGE_HASH_BYTES + 1 })
		{
			const core::Result<core::Hash256> refused = os::ReadCurrentWindowsProcessImageHash(limit);
			ASSERT_FALSE(refused);
			EXPECT_EQ(refused.GetError().Code, core::EErrorCode::InvalidArgument);
			EXPECT_EQ(refused.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-IMAGE-LIMIT");
		}
		const core::Result<core::Hash256> tooSmall = os::ReadCurrentWindowsProcessImageHash(1);
		ASSERT_FALSE(tooSmall);
		EXPECT_EQ(tooSmall.GetError().Code, core::EErrorCode::Unsupported);
		EXPECT_EQ(tooSmall.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-IMAGE-LIMIT");
		EXPECT_TRUE(os::ReadCurrentWindowsProcessImageHash());
	}
} // namespace
