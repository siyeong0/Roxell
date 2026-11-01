#include <Rhi.h>
#include <FailingMemoryResource.h>
#include <LinearMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(NullDeviceDeathTest, ManagedRegistration)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			ASSERT_TRUE(CreateDevice(EBackend::Null, memory, &device));
			EXPECT_DEATH((void)(device->RegisterManagedOwner(0)), "Managed registration requires a nonzero owner generation.");
			ASSERT_TRUE(device->RegisterManagedOwner(11));
			Result<void> duplicate = device->RegisterManagedOwner(12);
			ASSERT_TRUE(!duplicate && duplicate.GetError().Code == EErrorCode::Conflict);
			EXPECT_DEATH((void)(device->UnregisterManagedOwner(12)), "Only the registered managed owner releases its device exactly once.");
			EXPECT_DEATH((void)(device->SubmitManagedPrepared(12, {})), "Managed submission requires the registered owner generation.");
			device->UnregisterManagedOwner(11);
			EXPECT_DEATH((void)(device->UnregisterManagedOwner(11)), "Only the registered managed owner releases its device exactly once.");
			ASSERT_TRUE(device->RegisterManagedOwner(12));
			EXPECT_DEATH((void)(device->SubmitManagedPrepared(11, {})), "Managed submission requires the registered owner generation.");
			device->UnregisterManagedOwner(12);
		}
		ASSERT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(NullDeviceTest, DevicePreparationCache)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			ASSERT_TRUE(CreateDevice(EBackend::Null, memory, &device));
			Result<BufferHandle> buffer = device->CreateBuffer({ 8192, EBufferUsage::CopySource });
			ASSERT_TRUE(buffer);
			PreparedBufferAccessDesc access{ buffer.Value(), {0, 32}, EBufferState::CopySource };
			CommandAccessPhaseDesc phase;
			phase.Buffers = { &access, 1 };
			PreparedExecutionStatistics statistics;
			CommandListPreparationDesc list;
			list.Phases = { &phase, 1 };
			CommandListGroupPreparationDesc group;
			group.Lists = { &list, 1 };
			group.Statistics = &statistics;
			Result<CommandListHandle> first = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(first);
			list.List = first.Value();
			ASSERT_TRUE(device->PrepareCommandListGroup(group));
			ASSERT_TRUE(statistics.AccessProgramBuilds == 1 && statistics.AccessProgramReuses == 0);
			Result<CommandListHandle> second = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(second);
			list.List = second.Value();
			ASSERT_TRUE(device->PrepareCommandListGroup(group));
			ASSERT_TRUE(statistics.AccessProgramBuilds == 0 && statistics.AccessProgramReuses == 1);

			{
				// Exceed the cache entry budget while two executions retain their program.
				for (uint64 offset = 1; offset <= 4097; ++offset)
				{
					access.Range = { offset, 1 };
					Result<CommandListHandle> transient = device->CreateCommandList(EQueue::Copy);
					ASSERT_TRUE(transient);
					list.List = transient.Value();
					ASSERT_TRUE(device->PrepareCommandListGroup(group));
					ASSERT_TRUE(statistics.AccessProgramBuilds == 1 && statistics.AccessProgramReuses == 0);
					device->DiscardCommandList(transient.Value());
				}
				access.Range = { 0, 32 };
			}
			ASSERT_FALSE(device->DestroyBuffer(buffer.Value()));
			device->DiscardCommandList(first.Value());
			ASSERT_FALSE(device->DestroyBuffer(buffer.Value()));
			device->DiscardCommandList(second.Value());
			ASSERT_TRUE(device->DestroyBuffer(buffer.Value()));
			Result<BufferHandle> replacement = device->CreateBuffer({ 32, EBufferUsage::CopySource });
			ASSERT_TRUE(replacement && replacement.Value() != access.Buffer);
			access.Buffer = replacement.Value();
			Result<CommandListHandle> third = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(third);
			list.List = third.Value();
			ASSERT_TRUE(device->PrepareCommandListGroup(group));
			ASSERT_TRUE(statistics.AccessProgramBuilds == 1 && statistics.AccessProgramReuses == 0);
			device->DiscardCommandList(third.Value());
			ASSERT_TRUE(device->DestroyBuffer(replacement.Value()));
		}
		ASSERT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(NullDeviceDeathTest, CallerConfinedRecordingResource)
	{
		alignas(DEFAULT_MEMORY_ALIGNMENT) Byte storage[65536]{};
		LinearMemoryResource memory(storage, sizeof(storage));
		{
			UniquePtr<Device> device;
			Result<void> created = CreateDevice(EBackend::Null, memory, &device);
			ASSERT_TRUE(created);
			Result<void> rejected = device->BeginParallelRecording();
			ASSERT_TRUE(!rejected && rejected.GetError().Code == EErrorCode::Unsupported);
			EXPECT_DEATH((void)(device->EndParallelRecording()), "Join all recording workers before ending the active phase.");
			Result<BufferHandle> buffer = device->CreateBuffer({ 16, EBufferUsage::CopySource });
			ASSERT_TRUE(buffer && device->DestroyBuffer(buffer.Value()));
		}

		memory.Reset();
	}

	TEST(NullDeviceDeathTest, DeviceOutputOwnership)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		UniquePtr<Device> device;
		EXPECT_DEATH((void)(CreateDevice(EBackend::Null, memory, nullptr)), "Device creation requires an output owner.");
		ASSERT_FALSE(CreateDevice({ EBackend::Null, true }, memory, &device));
		ASSERT_TRUE(!device && memory.GetStatistics().LiveAllocationCount == 0);
		bool bCreated = false;
		for (usize capacity = 0; capacity < 32; ++capacity)
		{
			FailingMemoryResource failing(memory, capacity);
			Result<void> created = CreateDevice(EBackend::Null, failing, &device);
			if (created)
			{
				ASSERT_TRUE(device && device->GetBackend() == EBackend::Null);
				EXPECT_DEATH((void)(CreateDevice(EBackend::Null, failing, &device)), "Device creation requires an empty output owner.");
				device.Reset();
				bCreated = true;
			}
			ASSERT_TRUE(!device && memory.GetStatistics().LiveAllocationCount == 0);
			if (bCreated)
			{
				break;
			}
		}
		EXPECT_TRUE(bCreated);
	}
}
