#include <DynamicLibraryOwner.h>
#include <Utility.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>
#include <new>

using namespace rox::core;
using namespace rox::os;

namespace
{
	struct LibraryState final
	{
		bool bFailClose = true;
		int32* DestroyCount = nullptr;
	};

	Result<DynamicLibrarySymbol> findSymbol(void*, StringView) noexcept
	{
		return Result<DynamicLibrarySymbol>::Failure(EErrorCode::NotFound, "TEST-EXPORT", "Missing export.");
	}

	Result<void> closeLibrary(void* object) noexcept
	{
		LibraryState& state = *static_cast<LibraryState*>(object);
		if (state.bFailClose)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "TEST-CLOSE", "Close failed.");
		}
		return Result<void>::Success();
	}

	void destroyLibrary(MemoryResource& resource, void* object) noexcept
	{
		LibraryState* state = static_cast<LibraryState*>(object);
		++*state->DestroyCount;
		state->~LibraryState();
		resource.Deallocate(state, { sizeof(LibraryState), alignof(LibraryState) });
	}
}

TEST(DynamicLibraryOwnerTests, FailedCloseRetainsOwnerAndRetryDestroysExactlyOnce)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	void* storage = tracking.Allocate({ sizeof(LibraryState), alignof(LibraryState) });
	ASSERT_NE(storage, nullptr);
	int32 destroyCount = 0;
	LibraryState* state = new (storage) LibraryState{ true, &destroyCount };
	{
		DynamicLibraryOwner owner = DynamicLibraryOwner::Adopt(tracking, state, findSymbol, closeLibrary, destroyLibrary);
		const Result<void> failed = owner.Close();
		ASSERT_FALSE(failed);
		EXPECT_EQ(failed.GetError().Code, EErrorCode::ExternalFailure);
		EXPECT_EQ(failed.GetError().DiagnosticID, "TEST-CLOSE");
		EXPECT_TRUE(owner);
		EXPECT_EQ(destroyCount, 0);
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 1u);
		const Result<DynamicLibrarySymbol> symbol = owner.FindSymbol("valid");
		ASSERT_FALSE(symbol);
		EXPECT_EQ(symbol.GetError().DiagnosticID, "TEST-EXPORT");
		state->bFailClose = false;
		DynamicLibraryOwner moved(rox::core::Move(owner));
		EXPECT_FALSE(owner);
		EXPECT_TRUE(moved.Close());
		EXPECT_TRUE(moved.Close());
		EXPECT_FALSE(moved);
		EXPECT_EQ(destroyCount, 1);
	}
	EXPECT_EQ(destroyCount, 1);
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}

TEST(DynamicLibraryOwnerTests, InvalidExportNamesAreRejectedWithoutCallingBackend)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	void* storage = tracking.Allocate({ sizeof(LibraryState), alignof(LibraryState) });
	ASSERT_NE(storage, nullptr);
	int32 destroyCount = 0;
	LibraryState* state = new (storage) LibraryState{ false, &destroyCount };
	{
		DynamicLibraryOwner owner = DynamicLibraryOwner::Adopt(tracking, state, findSymbol, closeLibrary, destroyLibrary);
		const StringView invalid[] = { {}, StringView("a\0b", 3), StringView("\xff", 1) };
		for (const StringView name : invalid)
		{
			const Result<DynamicLibrarySymbol> result = owner.FindSymbol(name);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.GetError().Code, EErrorCode::InvalidArgument);
			EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-LIBRARY-SYMBOL");
		}
		EXPECT_TRUE(owner);
	}
	EXPECT_EQ(destroyCount, 1);
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}
