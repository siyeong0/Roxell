#include <Rhi.h>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(Device, CreationFailureLeavesTheOutputOwnerEmpty)
	{
		FailingMemoryResource memory(SystemMemoryResource::Get(), 0);
		UniquePtr<Device> device;
		const Result<void> result = CreateDevice(EBackend::Null, memory, &device);
		EXPECT_FALSE(result);
		EXPECT_FALSE(device);
	}

	TEST(Device, UncompiledBackendIsNotSilentlyReplaced)
	{
		UniquePtr<Device> device;
		const Result<void> result = CreateDevice(EBackend::D3D12, SystemMemoryResource::Get(), &device);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, EErrorCode::Unsupported);
		EXPECT_FALSE(device);
	}

	TEST(Device, PreparedExecutionsRetainResourcesUntilEveryListIsDiscarded)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			ASSERT_TRUE(CreateDevice(EBackend::Null, memory, &device));
			const Result<BufferHandle> buffer = device->CreateBuffer({ 64, EBufferUsage::CopySource });
			ASSERT_TRUE(buffer);
			const PreparedBufferAccessDesc access{ buffer.Value(), { 0, 32 }, EBufferState::CopySource };
			CommandAccessPhaseDesc phase;
			phase.Buffers = { &access, 1 };
			CommandListPreparationDesc list;
			list.Phases = { &phase, 1 };
			PreparedExecutionStatistics statistics;
			CommandListGroupPreparationDesc group;
			group.Lists = { &list, 1 };
			group.Statistics = &statistics;
			const Result<CommandListHandle> first = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(first);
			list.List = first.Value();
			ASSERT_TRUE(device->PrepareCommandListGroup(group));
			EXPECT_EQ(statistics.AccessProgramBuilds, 1u);
			const Result<CommandListHandle> second = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(second);
			list.List = second.Value();
			ASSERT_TRUE(device->PrepareCommandListGroup(group));
			EXPECT_EQ(statistics.AccessProgramReuses, 1u);
			EXPECT_FALSE(device->DestroyBuffer(buffer.Value()));
			device->DiscardCommandList(first.Value());
			EXPECT_FALSE(device->DestroyBuffer(buffer.Value()));
			device->DiscardCommandList(second.Value());
			ASSERT_TRUE(device->DestroyBuffer(buffer.Value()));
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
	}

	TEST(DeviceDeathTest, DestroyedHandleCannotAddressAReplacementResource)
	{
		UniquePtr<Device> device;
		ASSERT_TRUE(CreateDevice(EBackend::Null, SystemMemoryResource::Get(), &device));
		const Result<BufferHandle> buffer = device->CreateBuffer({ 64, EBufferUsage::CopySource });
		ASSERT_TRUE(buffer);
		ASSERT_TRUE(device->DestroyBuffer(buffer.Value()));
		const Result<BufferHandle> replacement = device->CreateBuffer({ 64, EBufferUsage::CopySource });
		ASSERT_TRUE(replacement);
		EXPECT_NE(buffer.Value(), replacement.Value());
		EXPECT_DEATH((void)device->DestroyBuffer(buffer.Value()), "");
		EXPECT_TRUE(device->DestroyBuffer(replacement.Value()));
	}

	TEST(DeviceDeathTest, OutputOwnerIsRequired)
	{
		EXPECT_DEATH((void)CreateDevice(EBackend::Null, SystemMemoryResource::Get(), nullptr), "output owner");
	}
}
