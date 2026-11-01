#include <Rhi.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(NullDeviceDeathTest, PermanentBufferState)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			Result<void> created = CreateDevice(EBackend::Null, memory, &device);
			ASSERT_TRUE(created);
			const EBufferUsage usage = EBufferUsage::CopySource | EBufferUsage::CopyDestination;
			Result<BufferHandle> source = device->CreateBuffer({ 16, usage });
			Result<BufferHandle> target = device->CreateBuffer({ 16, usage });
			ASSERT_TRUE(source && target);
			Byte bytes[4]{ Byte{1}, Byte{2}, Byte{3}, Byte{4} };
			Result<CommandListHandle> abandoned = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(abandoned);
			ASSERT_TRUE(device->SetPermanentBufferState(abandoned.Value(), source.Value(), EBufferState::CopySource));
			device->DiscardCommandList(abandoned.Value());
			Result<CommandListHandle> initial = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(initial);
			ASSERT_EQ(device->GetBufferState(initial.Value(), source.Value()).Value(), ETrackedResourceState::Unknown);
			ASSERT_TRUE(device->WriteBuffer(initial.Value(), source.Value(), 0, bytes));
			ASSERT_EQ(device->Submit(initial.Value()).Status, ESubmitStatus::Submitted);
			Result<CommandListHandle> permanent = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(permanent);
			ASSERT_TRUE(device->SetPermanentBufferState(permanent.Value(), source.Value(), EBufferState::CopySource));
			EXPECT_DEATH((void)((device->SetPermanentBufferState(permanent.Value(), source.Value(), EBufferState::CopySource))), "A command list sets each buffer's permanent state once.");
			device->DiscardCommandList(permanent.Value());
			permanent = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(permanent && device->SetPermanentBufferState(permanent.Value(), source.Value(), EBufferState::CopySource));
			ASSERT_EQ(device->Submit(permanent.Value()).Status, ESubmitStatus::Submitted);
			Result<CommandListHandle> use = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(use);
			ASSERT_EQ(device->GetBufferState(use.Value(), source.Value()).Value(), ETrackedResourceState::CopySource);
			EXPECT_DEATH((void)((device->SetBufferState(use.Value(), source.Value(), EBufferState::CopyDestination))), "The requested state must match the permanent buffer state.");
			device->DiscardCommandList(use.Value());
			use = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(use);
			ASSERT_TRUE(device->CopyBuffer(use.Value(), { source.Value(), target.Value(), 0, 0, 4 }));
			ASSERT_EQ(device->Submit(use.Value()).Status, ESubmitStatus::Submitted);
			Byte copied[4]{};
			ASSERT_TRUE(device->ReadBufferBlocking(target.Value(), 0, copied, {}, 10000));
			for (usize index = 0; index < 4; ++index)
			{
				ASSERT_EQ(copied[index], bytes[index]);
			}

			EXPECT_DEATH((void)device->UploadBuffer(source.Value(), 0, bytes), "permanent buffer state");
			Result<CommandListHandle> wrongUse = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(wrongUse);
			ASSERT_TRUE(device->WriteBuffer(wrongUse.Value(), source.Value(), 0, bytes));
			EXPECT_DEATH(([&] { (void)device->Submit(wrongUse.Value()); })(), "A buffer use must match its permanent state.");
			device->DiscardCommandList(wrongUse.Value());
			Result<CommandListHandle> conflict = device->CreateCommandList(EQueue::Copy);
			Result<CommandListHandle> other = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(conflict && other);
			ASSERT_TRUE(device->SetPermanentBufferState(conflict.Value(), target.Value(), EBufferState::CopyDestination));
			ASSERT_TRUE(device->CopyBuffer(other.Value(), { source.Value(), target.Value(), 0, 0, 4 }));
			EXPECT_DEATH(([&] { (void)device->Submit(conflict.Value()); })(), "Permanent transition requires exclusive recorded use and a matching state.");
			device->DiscardCommandList(conflict.Value());
			device->DiscardCommandList(other.Value());
			Result<CommandListHandle> afterConflict = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(afterConflict);
			ASSERT_EQ(device->GetBufferState(afterConflict.Value(), target.Value()).Value(), ETrackedResourceState::Unknown);
			device->DiscardCommandList(afterConflict.Value());
			Result<CommandListHandle> changedAfterPermanent = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(changedAfterPermanent);
			ASSERT_TRUE(device->SetPermanentBufferState(changedAfterPermanent.Value(), target.Value(), EBufferState::CopySource));
			ASSERT_TRUE(device->WriteBuffer(changedAfterPermanent.Value(), target.Value(), 0, bytes));
			EXPECT_DEATH(([&] { (void)device->Submit(changedAfterPermanent.Value()); })(), "A buffer cannot change state after its permanent transition.");
			device->DiscardCommandList(changedAfterPermanent.Value());
			Result<CommandListHandle> afterFailedSubmit = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(afterFailedSubmit);
			ASSERT_EQ(device->GetBufferState(afterFailedSubmit.Value(), target.Value()).Value(), ETrackedResourceState::Unknown);
			device->DiscardCommandList(afterFailedSubmit.Value());
			ASSERT_TRUE(device->DestroyBuffer(source.Value()));
			ASSERT_TRUE(device->DestroyBuffer(target.Value()));
		}

		ASSERT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(NullDeviceDeathTest, PermanentTextureState)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			Result<void> created = CreateDevice(EBackend::Null, memory, &device);
			ASSERT_TRUE(created);
			const TextureDesc desc{ 2, 2, EFormat::RGBA8_UNORM, ETextureUsage::CopySource | ETextureUsage::CopyDestination };
			Result<TextureHandle> source = device->CreateTexture(desc);
			Result<TextureHandle> target = device->CreateTexture(desc);
			ASSERT_TRUE(source && target);
			Byte pixels[16]{};
			for (usize index = 0; index < 16; ++index)
			{
				pixels[index] = static_cast<Byte>(index + 1);
			}

			Result<CommandListHandle> abandoned = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(abandoned);
			ASSERT_TRUE(device->SetPermanentTextureState(abandoned.Value(), source.Value(), ETextureState::CopySource));
			device->DiscardCommandList(abandoned.Value());
			ASSERT_TRUE(device->UploadTexture(source.Value(), pixels));
			Result<CommandListHandle> permanent = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(permanent);
			ASSERT_TRUE(device->SetPermanentTextureState(permanent.Value(), source.Value(), ETextureState::CopySource));
			EXPECT_DEATH((void)((device->SetPermanentTextureState(permanent.Value(), source.Value(), ETextureState::CopySource))), "A command list sets each texture's permanent state once.");
			device->DiscardCommandList(permanent.Value());
			permanent = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(permanent && device->SetPermanentTextureState(permanent.Value(), source.Value(), ETextureState::CopySource));
			ASSERT_EQ(device->Submit(permanent.Value()).Status, ESubmitStatus::Submitted);
			Result<CommandListHandle> use = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(use);
			ASSERT_EQ(device->GetTextureSubresourceState(use.Value(), source.Value(), {}).Value(), ETrackedResourceState::CopySource);
			EXPECT_DEATH((void)((device->SetTextureState(use.Value(), source.Value(), ETextureState::CopyDestination))), "The requested state must match the permanent texture state.");
			device->DiscardCommandList(use.Value());
			use = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(use);
			ASSERT_TRUE(device->CopyTexture(use.Value(), { source.Value(), target.Value() }));
			ASSERT_EQ(device->Submit(use.Value()).Status, ESubmitStatus::Submitted);
			Byte copied[16]{};
			ASSERT_TRUE(device->ReadTextureBlocking(target.Value(), copied, {}, 10000));
			for (usize index = 0; index < 16; ++index)
			{
				ASSERT_EQ(copied[index], pixels[index]);
			}

			EXPECT_DEATH((void)device->UploadTexture(source.Value(), pixels), "permanent texture state");
			Result<CommandListHandle> wrongUse = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(wrongUse);
			ASSERT_TRUE(device->WriteTexture(wrongUse.Value(), source.Value(), {}, pixels));
			EXPECT_DEATH(([&] { (void)device->Submit(wrongUse.Value()); })(), "A texture use must match its permanent state.");
			device->DiscardCommandList(wrongUse.Value());
			Result<CommandListHandle> changedAfterPermanent = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(changedAfterPermanent);
			ASSERT_TRUE(device->SetPermanentTextureState(changedAfterPermanent.Value(), target.Value(), ETextureState::CopySource));
			ASSERT_TRUE(device->WriteTexture(changedAfterPermanent.Value(), target.Value(), {}, pixels));
			EXPECT_DEATH(([&] { (void)device->Submit(changedAfterPermanent.Value()); })(), "A texture cannot change state after its permanent transition.");
			device->DiscardCommandList(changedAfterPermanent.Value());
			ASSERT_TRUE(device->UploadTexture(target.Value(), pixels));
			ASSERT_TRUE(device->DestroyTexture(source.Value()));
			ASSERT_TRUE(device->DestroyTexture(target.Value()));
		}

		ASSERT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(NullDeviceDeathTest, EventQuery)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			Result<void> created = CreateDevice(EBackend::Null, memory, &device);
			ASSERT_TRUE(created);
			Result<EventQueryHandle> query = device->CreateEventQuery();
			ASSERT_TRUE(query);
			ASSERT_FALSE(device->PollEventQuery(query.Value()).Value());
			ASSERT_TRUE(device->WaitEventQuery(query.Value(), 0));
			ASSERT_FALSE(device->SetEventQuery(query.Value(), EQueue::Compute));
			EXPECT_DEATH((void)(device->SetEventQuery(query.Value(), static_cast<EQueue>(255))), "The queue kind is invalid.");
			ASSERT_TRUE(device->SetEventQuery(query.Value(), EQueue::Copy));
			ASSERT_TRUE(device->PollEventQuery(query.Value()).Value());
			EXPECT_DEATH((void)((device->SetEventQuery(query.Value(), EQueue::Copy))), "Reset the event query before setting it again.");
			ASSERT_TRUE(device->ResetEventQuery(query.Value()));
			ASSERT_FALSE(device->PollEventQuery(query.Value()).Value());
			Result<BufferHandle> buffer = device->CreateBuffer({ 4, EBufferUsage::CopySource | EBufferUsage::CopyDestination });
			ASSERT_TRUE(buffer);
			const uint32 value = 17;
			ASSERT_TRUE(device->UploadBuffer(buffer.Value(), 0, { reinterpret_cast<const Byte*>(&value), sizeof(value) }));
			ASSERT_TRUE(device->SetEventQuery(query.Value(), EQueue::Copy));
			ASSERT_TRUE(device->PollEventQuery(query.Value()).Value());
			ASSERT_TRUE(device->WaitEventQuery(query.Value(), 10000));
			ASSERT_TRUE(device->DestroyEventQuery(query.Value()));
			EXPECT_DEATH(([&] { (void)(device->PollEventQuery(query.Value())); })(), "handle");
			Result<EventQueryHandle> reused = device->CreateEventQuery();
			ASSERT_TRUE(reused && reused.Value().Slot == query.Value().Slot && reused.Value().Generation != query.Value().Generation);
			ASSERT_TRUE(device->DestroyEventQuery(reused.Value()));
			ASSERT_TRUE(device->DestroyBuffer(buffer.Value()));
		}

		ASSERT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(NullDeviceDeathTest, RecordingContracts)
	{
		UniquePtr<Device> createdOwner;
		Result<void> created = CreateDevice(EBackend::Null, SystemMemoryResource::Get(), &createdOwner);
		ASSERT_TRUE(created);
		Device& device = *createdOwner;
		Result<CommandListHandle> commands = device.CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(commands);
		EXPECT_DEATH((void)(device.EndRendering(commands.Value())), "EndRendering requires an active rendering pass.");
		SubmitOutcome submitted = device.Submit(commands.Value());
		ASSERT_TRUE(submitted.Status == ESubmitStatus::Submitted && device.WaitForSubmission(submitted.Token, 1000));
	}
}
