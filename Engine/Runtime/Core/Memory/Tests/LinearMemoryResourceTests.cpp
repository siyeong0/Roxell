#include <LinearMemoryResource.h>

#include <gtest/gtest.h>
#include <cstdlib>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

using namespace rox::core;

namespace
{
	void suppressAssertionDialog() noexcept
	{
#if defined(_WIN32)
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
	}
}

TEST(LinearMemoryResourceTests, NestedMarkersRestoreCursorAndPreservePeak)
{
	alignas(64) Byte storage[128]{};
	LinearMemoryResource resource(storage, sizeof(storage));
	EXPECT_EQ(resource.GetThreadSafety(), EMemoryResourceThreadSafety::CallerConfined);
	EXPECT_EQ(resource.GetCapacityByteCount(), sizeof(storage));

	void* const first = resource.Allocate({5, 1});
	EXPECT_EQ(first, storage);
	const LinearMemoryResource::Marker outer = resource.Mark();
	void* const aligned = resource.Allocate({16, 32});
	EXPECT_EQ(aligned, storage + 32);
	const LinearMemoryResource::Marker inner = resource.Mark();
	void* const temporary = resource.Allocate({8, 8});
	EXPECT_EQ(temporary, storage + 48);
	EXPECT_EQ(resource.Allocate({128, 1}), nullptr);
	EXPECT_EQ(resource.GetUsedByteCount(), 56);
	resource.Rewind(inner);
	EXPECT_EQ(resource.GetUsedByteCount(), 48);
	EXPECT_EQ(resource.Allocate({8, 8}), temporary);
	resource.Rewind(outer);
	EXPECT_EQ(resource.GetUsedByteCount(), 5);
	EXPECT_EQ(resource.GetPeakByteCount(), 56);

	resource.Reset();
	EXPECT_EQ(resource.GetUsedByteCount(), 0);
	EXPECT_EQ(resource.GetPeakByteCount(), 56);
	EXPECT_EQ(resource.Allocate({5, 1}), first);
}

TEST(LinearMemoryResourceTests, EmptyBackingRejectsNonzeroAllocation)
{
	LinearMemoryResource resource(nullptr, 0);
	EXPECT_EQ(resource.Allocate({0, 1}), nullptr);
	EXPECT_EQ(resource.Allocate({1, 1}), nullptr);
	EXPECT_EQ(resource.GetUsedByteCount(), 0);
	EXPECT_EQ(resource.GetPeakByteCount(), 0);
}

TEST(LinearMemoryResourceTests, PaddingExhaustionAndDeallocationDoNotChangeCursor)
{
	alignas(64) Byte storage[32]{};
	LinearMemoryResource resource(storage + 1, 31);
	void* const first = resource.Allocate({1, 1});
	ASSERT_EQ(first, storage + 1);
	resource.Deallocate(first, {1, 1});
	EXPECT_EQ(resource.GetUsedByteCount(), 1);
	EXPECT_EQ(resource.Allocate({16, 16}), storage + 16);
	EXPECT_EQ(resource.GetUsedByteCount(), 31);
	EXPECT_EQ(resource.Allocate({1, 1}), nullptr);
	EXPECT_EQ(resource.GetPeakByteCount(), 31);
	resource.Reset();
	EXPECT_EQ(resource.Allocate({17, 16}), nullptr);
	EXPECT_EQ(resource.GetUsedByteCount(), 0);
	EXPECT_EQ(resource.GetPeakByteCount(), 31);
}

TEST(LinearMemoryResourceTests, MarkersAtSameCursorCanBeRewoundInOrder)
{
	alignas(16) Byte storage[32]{};
	LinearMemoryResource resource(storage, sizeof(storage));
	const auto outer = resource.Mark();
	const auto inner = resource.Mark();
	EXPECT_NE(resource.Allocate({8, 1}), nullptr);
	resource.Rewind(inner);
	EXPECT_EQ(resource.GetUsedByteCount(), 0);
	EXPECT_NE(resource.Allocate({4, 1}), nullptr);
	resource.Rewind(outer);
	EXPECT_EQ(resource.GetUsedByteCount(), 0);
	EXPECT_EQ(resource.GetPeakByteCount(), 8);
	resource.Reset();
	EXPECT_EQ(resource.Allocate({8, 1}), storage);
}

TEST(LinearMemoryResourceTests, ForeignAndOutOfOrderMarkersAreRejected)
{
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			Byte firstStorage[8]{};
			Byte secondStorage[8]{};
			LinearMemoryResource first(firstStorage, sizeof(firstStorage));
			LinearMemoryResource second(secondStorage, sizeof(secondStorage));
			second.Rewind(first.Mark());
		},
		"A linear memory marker must belong to this resource");
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			Byte storage[8]{};
			LinearMemoryResource resource(storage, sizeof(storage));
			const auto outer = resource.Mark();
			(void)resource.Mark();
			resource.Rewind(outer);
		},
		"Linear memory markers must be consumed once in reverse creation order");
}

TEST(LinearMemoryResourceTests, ResetWithActiveMarkerIsRejected)
{
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			Byte storage[8]{};
			LinearMemoryResource resource(storage, sizeof(storage));
			(void)resource.Mark();
			resource.Reset();
		},
		"A linear memory reset requires no active markers");
}
