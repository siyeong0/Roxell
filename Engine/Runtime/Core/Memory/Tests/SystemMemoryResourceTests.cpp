#include <MemoryBlock.h>
#include <NumericLimits.hpp>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(SystemMemoryResourceTests, AlignmentAndOverflow)
{
	SystemMemoryResource& resource = SystemMemoryResource::Get();
	EXPECT_EQ(&resource, &SystemMemoryResource::Get());
	EXPECT_EQ(resource.GetThreadSafety(), EMemoryResourceThreadSafety::ThreadSafe);
	EXPECT_EQ(resource.Allocate({0, 64}), nullptr);
	resource.Deallocate(nullptr, {0, 64});

	MemoryBlock block = MemoryBlock::Create(resource, {37, 256});
	ASSERT_FALSE(block.IsEmpty());
	EXPECT_EQ(reinterpret_cast<uintptr>(block.GetData()) % 256, 0);
	EXPECT_EQ(block.GetByteCount(), 37);
	EXPECT_EQ(resource.Allocate({NumericLimits<usize>::max(), 16}), nullptr);
}
