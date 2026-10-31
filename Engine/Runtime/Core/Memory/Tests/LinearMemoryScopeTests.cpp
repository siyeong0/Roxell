#include <LinearMemoryScope.hpp>

#include <gtest/gtest.h>
#include <type_traits>

using namespace rox::core;

static_assert(!std::is_copy_constructible_v<LinearMemoryScope>);
static_assert(!std::is_move_constructible_v<LinearMemoryScope>);
static_assert(!std::is_copy_assignable_v<LinearMemoryScope>);
static_assert(!std::is_move_assignable_v<LinearMemoryScope>);

namespace
{
	struct ScopeOwner final
	{
		LinearMemoryResource& Resource;
		usize& UsedAtDestruction;

		~ScopeOwner() noexcept
		{
			UsedAtDestruction = Resource.GetUsedByteCount();
		}
	};
}

TEST(LinearMemoryScopeTests, NestedScopesRewindAfterTemporaryOwnersAreDestroyed)
{
	alignas(16) Byte storage[64]{};
	LinearMemoryResource resource(storage, sizeof(storage));
	EXPECT_EQ(resource.Allocate({8, 1}), storage);
	usize usedAtDestruction = 0;
	{
		LinearMemoryScope outer(resource);
		EXPECT_EQ(resource.Allocate({8, 1}), storage + 8);
		{
			LinearMemoryScope inner(resource);
			EXPECT_EQ(resource.Allocate({8, 1}), storage + 16);
			ScopeOwner owner{resource, usedAtDestruction};
		}
		EXPECT_EQ(usedAtDestruction, 24);
		EXPECT_EQ(resource.GetUsedByteCount(), 16);
		EXPECT_EQ(resource.Allocate({8, 1}), storage + 16);
	}
	EXPECT_EQ(resource.GetUsedByteCount(), 8);
	EXPECT_EQ(resource.GetPeakByteCount(), 24);
	EXPECT_EQ(resource.Allocate({8, 1}), storage + 8);
}
