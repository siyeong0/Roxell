#include <Rhi.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(NullDeviceDeathTest, Texture3DCopy)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			Result<void> created = CreateDevice(EBackend::Null, memory, &device);
			ASSERT_TRUE(created);
			const ETextureUsage usage = ETextureUsage::CopySource | ETextureUsage::CopyDestination;
			const TextureDesc desc{ 2, 2, EFormat::RGBA8_UNORM, usage, 2, 1, 1, ETextureDimension::Texture3D, 2 };
			TextureDesc invalidAttachment = desc;
			invalidAttachment.Usage = usage | ETextureUsage::RenderTarget;
			ASSERT_FALSE(device->CreateTexture(invalidAttachment));
			Result<TextureHandle> source = device->CreateTexture(desc);
			Result<TextureHandle> destination = device->CreateTexture(desc);
			ASSERT_TRUE(source && destination);
			Byte first[32]{};
			Byte rewritten[32]{};
			Byte second[4]{};
			for (usize index = 0; index < 32; ++index)
			{
				first[index] = static_cast<Byte>(index + 1);
				rewritten[index] = static_cast<Byte>(100 + index);
			}

			for (usize index = 0; index < 4; ++index)
			{
				second[index] = static_cast<Byte>(200 + index);
			}

			ASSERT_TRUE(device->UploadTextureSubresource(source.Value(), { 0, 0 }, first));
			ASSERT_TRUE(device->UploadTextureSubresource(source.Value(), { 1, 0 }, second));
			EXPECT_DEATH((void)((device->UploadTextureSubresource(source.Value(), { 0, 0 }, second))), "Texture upload usage or tightly packed byte count is invalid.");
			Result<TextureBufferFootprint> footprint = device->GetTextureBufferFootprint(source.Value(), {});
			ASSERT_TRUE(footprint && footprint.Value().RowPitchBytes == 256 && footprint.Value().RowCount == 2 && footprint.Value().Depth == 2 && footprint.Value().ByteCount == 1024);
			Result<CommandListHandle> list = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(list);
			TextureCopyRegion region{};
			region.Source = source.Value();
			region.Destination = destination.Value();
			region.Width = 1;
			region.Height = 1;
			region.SourceZ = 1;
			region.Depth = 2;
			EXPECT_DEATH((void)((device->CopyTextureRegion(list.Value(), region))), "The texture region is empty or outside a subresource.");
			ASSERT_TRUE(device->WriteTexture(list.Value(), source.Value(), { 0, 0 }, rewritten));
			ASSERT_TRUE(device->CopyTexture(list.Value(), { source.Value(), destination.Value() }));
			SubmitOutcome submitted = device->Submit(list.Value());
			ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
			Byte firstOutput[32]{};
			Byte secondOutput[4]{};
			ASSERT_TRUE(device->ReadTextureSubresourceBlocking(destination.Value(), { 0, 0 }, firstOutput, submitted.Token, 0));
			ASSERT_TRUE(device->ReadTextureSubresourceBlocking(destination.Value(), { 1, 0 }, secondOutput, submitted.Token, 0));
			for (usize index = 0; index < 32; ++index)
			{
				ASSERT_EQ(firstOutput[index], rewritten[index]);
			}

			for (usize index = 0; index < 4; ++index)
			{
				ASSERT_EQ(secondOutput[index], second[index]);
			}

			Byte zeros[32]{};
			ASSERT_TRUE(device->UploadTextureSubresource(destination.Value(), {}, zeros));
			region.SourceX = 1;
			region.Width = 1;
			region.Height = 2;
			region.Depth = 1;
			region.DestinationZ = 0;
			Result<CommandListHandle> regionList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(regionList);
			ASSERT_TRUE(device->CopyTextureRegion(regionList.Value(), region));
			SubmitOutcome regionSubmission = device->Submit(regionList.Value());
			ASSERT_EQ(regionSubmission.Status, ESubmitStatus::Submitted);
			ASSERT_TRUE(device->ReadTextureSubresourceBlocking(destination.Value(), {}, firstOutput, regionSubmission.Token, 0));
			for (usize index = 0; index < 32; ++index)
			{
				const usize row = index / 8;
				const usize column = index % 8;
				const Byte expected =
					row < 2 && column < 4 ? rewritten[16 + row * 8 + 4 + column] : Byte{ 0 };
				ASSERT_EQ(firstOutput[index], expected);
			}

			Result<BufferHandle> upload = device->CreateBuffer({ 1536, EBufferUsage::CopySource, EBufferMemory::CpuWrite });
			Result<BufferHandle> readback =
				device->CreateBuffer({ 1024, EBufferUsage::CopyDestination, EBufferMemory::CpuRead });
			ASSERT_TRUE(upload && readback);
			Result<Span<Byte>> mapped = device->MapBuffer(upload.Value(), 512, 1024, 0);
			ASSERT_TRUE(mapped);

			for (usize row = 0; row < 4; ++row)
			{
				for (usize index = 0; index < 8; ++index)
				{
					mapped.Value()[row * 256 + index] = static_cast<Byte>(10 + row * 8 + index);
				}
			}

			ASSERT_TRUE(device->UnmapBuffer(upload.Value()));
			Result<CommandListHandle> bufferList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(bufferList);
			EXPECT_DEATH((void)((device->CopyBufferToTexture(bufferList.Value(), { upload.Value(), destination.Value(), {}, 1 }))), "Rendering state, usage, mapping, or buffer alignment is invalid.");
			ASSERT_TRUE(device->CopyBufferToTexture(bufferList.Value(), { upload.Value(), destination.Value(), {}, 512 }));
			ASSERT_TRUE(device->CopyTextureToBuffer(bufferList.Value(), { readback.Value(), destination.Value(), {}, 0 }));
			SubmitOutcome bufferSubmission = device->Submit(bufferList.Value());
			ASSERT_TRUE(bufferSubmission.Status == ESubmitStatus::Submitted && device->WaitForSubmission(bufferSubmission.Token, 0));
			Result<Span<Byte>> result = device->MapBuffer(readback.Value(), 0, 1024, 0);
			ASSERT_TRUE(result);

			for (usize row = 0; row < 4; ++row)
			{
				for (usize index = 0; index < 8; ++index)
				{
					ASSERT_EQ(result.Value()[row * 256 + index], static_cast<Byte>(10 + row * 8 + index));
				}
			}

			ASSERT_TRUE(device->UnmapBuffer(readback.Value()));
			ASSERT_TRUE(device->ReadTextureSubresourceBlocking(destination.Value(), {}, firstOutput, bufferSubmission.Token, 0));
			for (usize index = 0; index < 32; ++index)
			{
				ASSERT_EQ(firstOutput[index], static_cast<Byte>(10 + index));
			}

			Result<StagingTextureHandle> stagingWrite = device->CreateStagingTexture(desc, EBufferMemory::CpuWrite);
			Result<StagingTextureHandle> stagingRead = device->CreateStagingTexture(desc, EBufferMemory::CpuRead);
			ASSERT_TRUE(stagingWrite && stagingRead);
			Result<StagingTextureMapping> mappedStaging = device->MapStagingTexture(stagingWrite.Value(), {}, 0);
			ASSERT_TRUE(mappedStaging && mappedStaging.Value().Footprint.Depth == 2 && mappedStaging.Value().Bytes.Size() == 1024);

			for (usize row = 0; row < 4; ++row)
			{
				for (usize index = 0; index < 8; ++index)
				{
					mappedStaging.Value().Bytes[row * 256 + index] = static_cast<Byte>(50 + row * 8 + index);
				}
			}

			ASSERT_TRUE(device->UnmapStagingTexture(stagingWrite.Value()));
			Result<CommandListHandle> stagingList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(stagingList);
			ASSERT_TRUE(device->CopyStagingTextureToTexture(stagingList.Value(), stagingWrite.Value(), {}, destination.Value(), {}));
			ASSERT_TRUE(device->CopyTextureToStagingTexture(stagingList.Value(), destination.Value(), {}, stagingRead.Value(), {}));
			SubmitOutcome stagingSubmitted = device->Submit(stagingList.Value());
			ASSERT_TRUE(stagingSubmitted.Status == ESubmitStatus::Submitted && device->WaitForSubmission(stagingSubmitted.Token, 0));
			Result<StagingTextureMapping> staged = device->MapStagingTexture(stagingRead.Value(), {}, 0);
			ASSERT_TRUE(staged);

			for (usize row = 0; row < 4; ++row)
			{
				for (usize index = 0; index < 8; ++index)
				{
					ASSERT_EQ(staged.Value().Bytes[row * 256 + index], static_cast<Byte>(50 + row * 8 + index));
				}
			}

			ASSERT_TRUE(device->UnmapStagingTexture(stagingRead.Value()));
			ASSERT_TRUE(device->DestroyStagingTexture(stagingWrite.Value()));
			ASSERT_TRUE(device->DestroyStagingTexture(stagingRead.Value()));
			ASSERT_TRUE(device->DestroyBuffer(upload.Value()));
			ASSERT_TRUE(device->DestroyBuffer(readback.Value()));
			ASSERT_TRUE(device->DestroyTexture(source.Value()));
			ASSERT_TRUE(device->DestroyTexture(destination.Value()));
		}

		ASSERT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(NullDeviceDeathTest, Texture1D)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			Result<void> created = CreateDevice(EBackend::Null, memory, &device);
			ASSERT_TRUE(created);
			const ETextureUsage usage =
				ETextureUsage::CopySource | ETextureUsage::CopyDestination | ETextureUsage::RenderTarget;
			const TextureDesc desc{ 4, 1, EFormat::RGBA8_UNORM, usage, 2, 2, 1, ETextureDimension::Texture1D };
			TextureDesc invalid = desc;
			invalid.Height = 2;
			EXPECT_DEATH((void)((device->CreateTexture(invalid))), "A valid texture descriptor is required.");
			invalid = desc;
			invalid.SampleCount = 2;
			ASSERT_FALSE(device->CreateTexture(invalid));
			invalid = desc;
			invalid.Format = EFormat::BC1_UNORM;
			ASSERT_FALSE(device->CreateTexture(invalid));
			Result<TextureHandle> source = device->CreateTexture(desc);
			Result<TextureHandle> destination = device->CreateTexture(desc);
			ASSERT_TRUE(source && destination);
			Byte pixels[16]{};
			for (usize index = 0; index < 16; ++index)
			{
				pixels[index] = static_cast<Byte>(index + 1);
			}

			ASSERT_TRUE(device->UploadTextureSubresource(source.Value(), { 0, 1 }, pixels));
			Byte small[8]{};
			for (usize index = 0; index < 8; ++index)
			{
				small[index] = static_cast<Byte>(50 + index);
			}

			ASSERT_TRUE(device->UploadTextureSubresource(source.Value(), { 1, 1 }, small));
			Result<TextureBufferFootprint> footprint = device->GetTextureBufferFootprint(source.Value(), { 0, 1 });
			ASSERT_TRUE(
				footprint &&
				footprint.Value().Width == 4 &&
				footprint.Value().Height == 1 &&
				footprint.Value().Depth == 1 &&
				footprint.Value().RowPitchBytes == 256 &&
				footprint.Value().ByteCount == 256);
			Result<CommandListHandle> copyList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(copyList);
			ASSERT_TRUE(device->CopyTexture(copyList.Value(), { source.Value(), destination.Value() }));
			SubmitOutcome copied = device->Submit(copyList.Value());
			ASSERT_EQ(copied.Status, ESubmitStatus::Submitted);
			Byte output[16]{};
			ASSERT_TRUE(device->ReadTextureSubresourceBlocking(destination.Value(), { 0, 1 }, output, copied.Token, 0));
			for (usize index = 0; index < 16; ++index)
			{
				ASSERT_EQ(output[index], pixels[index]);
			}

			Result<CommandListHandle> regionList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(regionList);
			TextureCopyRegion region{};
			region.Source = source.Value();
			region.Destination = destination.Value();
			region.SourceSubresource = { 0, 1 };
			region.DestinationSubresource = { 0, 0 };
			region.SourceX = 1;
			region.Width = 2;
			region.Height = 1;
			ASSERT_TRUE(device->CopyTextureRegion(regionList.Value(), region));
			SubmitOutcome regionSubmission = device->Submit(regionList.Value());
			ASSERT_EQ(regionSubmission.Status, ESubmitStatus::Submitted);
			ASSERT_TRUE(device->ReadTextureSubresourceBlocking(destination.Value(), { 0, 0 }, output, regionSubmission.Token, 0));
			for (usize index = 0; index < 8; ++index)
			{
				ASSERT_EQ(output[index], pixels[index + 4]);
			}

			Result<CommandListHandle> clearList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(clearList);
			ASSERT_TRUE(device->ClearColorTextureSubresource(clearList.Value(), source.Value(), { 1, 1 }, ClearColor::Float(1, 0, 0, 1)));
			SubmitOutcome cleared = device->Submit(clearList.Value());
			ASSERT_EQ(cleared.Status, ESubmitStatus::Submitted);
			ASSERT_TRUE(device->ReadTextureSubresourceBlocking(source.Value(), { 1, 1 }, small, cleared.Token, 0));
			for (usize index = 0; index < 8; index += 4)
			{
				ASSERT_TRUE(small[index] == Byte{ 255 } && small[index + 3] == Byte{ 255 });
			}

			ASSERT_TRUE(device->DestroyTexture(source.Value()));
			ASSERT_TRUE(device->DestroyTexture(destination.Value()));
		}

		ASSERT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(NullDeviceDeathTest, TextureBufferTransfer)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> rejectedDeviceOwner3;
			ASSERT_FALSE(CreateDevice({ EBackend::Null, true }, memory, &rejectedDeviceOwner3));
			UniquePtr<Device> rejectedDeviceOwner2;
			ASSERT_FALSE(CreateDevice({ EBackend::Null, false, EAdapterPreference::Software }, memory, &rejectedDeviceOwner2));
			EXPECT_DEATH((void)CreateDevice({ EBackend::Null, false, static_cast<EAdapterPreference>(255) }, memory, &rejectedDeviceOwner2), "adapter preference");
			UniquePtr<Device> device;
			Result<void> created = CreateDevice(EBackend::Null, memory, &device);
			ASSERT_TRUE(created);
			const TextureDesc desc{ 3, 2, EFormat::RGBA8_UNORM, ETextureUsage::CopySource | ETextureUsage::CopyDestination, 2, 2 };
			EXPECT_DEATH((void)((device->QueryTextureResolveSupport(desc, static_cast<ETextureResolveMode>(255)))), "The resolve query format and modes must name compatible active planes.");
			TextureDesc invalidDesc = desc;
			invalidDesc.Width = 0;
			EXPECT_DEATH((void)((device->QueryTextureResolveSupport(invalidDesc, ETextureResolveMode::Average))), "A valid texture descriptor is required.");
			EXPECT_DEATH((void)((device->CreateStagingTexture(invalidDesc, EBufferMemory::CpuRead))), "A valid texture descriptor is required.");
			Result<bool> resolveSupport = device->QueryTextureResolveSupport(desc, ETextureResolveMode::Average);
			ASSERT_TRUE(resolveSupport && !resolveSupport.Value());
			Result<TextureHandle> texture = device->CreateTexture(desc);
			ASSERT_TRUE(texture);
			const TextureSubresource subresource{ 0, 1 };
			Result<TextureBufferFootprint> footprint = device->GetTextureBufferFootprint(texture.Value(), subresource);
			ASSERT_TRUE(footprint);
			ASSERT_EQ(footprint.Value().RowPitchBytes, 256);
			ASSERT_EQ(footprint.Value().RowCount, 2);
			ASSERT_EQ(footprint.Value().ByteCount, 512);
			EXPECT_DEATH((void)((device->GetTextureBufferFootprint(texture.Value(), { 2, 0 }))), "The texture footprint subresource must be valid.");
			Result<CommandListHandle> stateList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(stateList);
			ASSERT_TRUE(device->SetEnableAutomaticBarriers(stateList.Value(), false));
			ASSERT_TRUE(device->SetEnableAutomaticBarriers(stateList.Value(), true));
			Result<BufferHandle> stateBuffer =
				device->CreateBuffer({ 16, EBufferUsage::CopySource | EBufferUsage::CopyDestination });
			ASSERT_TRUE(stateBuffer);
			Result<CommandListStatistics> statistics = device->GetCommandListStatistics(stateList.Value());
			ASSERT_TRUE(statistics && statistics.Value().BarrierCalls == 0 && statistics.Value().BarrierCount == 0 && statistics.Value().DescriptorCount == 0);
			EXPECT_DEATH(([&] { (void)(device->GetCommandListStatistics({})); })(), "handle");
			Result<CommandListHandle> unsupportedBufferPolicy = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(unsupportedBufferPolicy);
			EXPECT_DEATH(
				(void)((device->SetEnableUavBarriersForBuffer(unsupportedBufferPolicy.Value(), stateBuffer.Value(), false))),
				"A non-mapped storage buffer and graphics or compute list are required.");
			device->DiscardCommandList(unsupportedBufferPolicy.Value());
			Result<CommandListHandle> unsupportedTexturePolicy = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(unsupportedTexturePolicy);
			EXPECT_DEATH((void)((device->SetEnableUavBarriersForTexture(unsupportedTexturePolicy.Value(), texture.Value(), false))), "A storage texture and graphics or compute list are required.");
			device->DiscardCommandList(unsupportedTexturePolicy.Value());
			ASSERT_EQ(device->GetBufferState(stateList.Value(), stateBuffer.Value()).Value(), ETrackedResourceState::Unknown);
			ASSERT_TRUE(device->BeginTrackingBufferState(stateList.Value(), stateBuffer.Value(), EBufferState::CopySource));
			ASSERT_EQ(device->GetBufferState(stateList.Value(), stateBuffer.Value()).Value(), ETrackedResourceState::CopySource);
			ASSERT_TRUE(device->SetBufferState(stateList.Value(), stateBuffer.Value(), EBufferState::CopyDestination));
			Result<CommandListHandle> badBufferTracking = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(badBufferTracking && device->SetBufferState(badBufferTracking.Value(), stateBuffer.Value(), EBufferState::CopyDestination));
			EXPECT_DEATH((void)((device->BeginTrackingBufferState(badBufferTracking.Value(), stateBuffer.Value(), EBufferState::CopyDestination))), "Begin tracking before the first buffer use.");
			device->DiscardCommandList(badBufferTracking.Value());
			ASSERT_EQ(device->GetBufferState(stateList.Value(), stateBuffer.Value()).Value(), ETrackedResourceState::CopyDestination);
			Result<BufferHandle> stateTarget =
				device->CreateBuffer({ 16, EBufferUsage::CopySource | EBufferUsage::CopyDestination });
			ASSERT_TRUE(stateTarget);
			ASSERT_TRUE(device->CopyBuffer(stateList.Value(), { stateBuffer.Value(), stateTarget.Value(), 0, 0, 4 }));
			ASSERT_EQ(device->GetBufferState(stateList.Value(), stateBuffer.Value()).Value(), ETrackedResourceState::CopySource);
			ASSERT_EQ(device->GetBufferState(stateList.Value(), stateTarget.Value()).Value(), ETrackedResourceState::CopyDestination);
			EXPECT_DEATH((void)((device->SetBufferState(stateList.Value(), stateBuffer.Value(), EBufferState::Storage))), "The state, usage, queue, or memory type is invalid.");
			ASSERT_TRUE(device->BeginTrackingTextureState(stateList.Value(), texture.Value(), subresource, ETextureState::CopySource));
			ASSERT_TRUE(device->SetTextureState(stateList.Value(), texture.Value(), subresource, ETextureState::CopyDestination));
			ASSERT_EQ(device->GetTextureSubresourceState(stateList.Value(), texture.Value(), subresource).Value(), ETrackedResourceState::Unknown);
			EXPECT_DEATH((void)((device->GetTextureSubresourceState(stateList.Value(), texture.Value(), { 2, 0 }))), "The queried texture subresource must be valid.");
			EXPECT_DEATH((void)((device->SetTextureState(stateList.Value(), texture.Value(), subresource, ETextureState::ShaderRead))), "The state, usage, queue, or subresource is invalid.");
			ASSERT_TRUE(device->CommitBarriers(stateList.Value()));
			device->DiscardCommandList(stateList.Value());
			Result<CommandListHandle> badTextureTracking = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(badTextureTracking && device->SetTextureState(badTextureTracking.Value(), texture.Value(), subresource, ETextureState::CopyDestination));
			EXPECT_DEATH(
				(void)((device->BeginTrackingTextureState(badTextureTracking.Value(), texture.Value(), subresource, ETextureState::CopyDestination))),
				"Begin tracking before the first texture use.");
			device->DiscardCommandList(badTextureTracking.Value());
			EXPECT_DEATH(([&] { (void)(device->SetEnableAutomaticBarriers(stateList.Value(), false)); })(), "handle");
			EXPECT_DEATH(([&] { (void)(device->CommitBarriers(stateList.Value())); })(), "handle");
			EXPECT_DEATH(([&] { (void)(device->GetBufferState(stateList.Value(), stateBuffer.Value())); })(), "handle");
			ASSERT_TRUE(device->DestroyBuffer(stateTarget.Value()));
			ASSERT_TRUE(device->DestroyBuffer(stateBuffer.Value()));
			Result<BufferHandle> upload = device->CreateBuffer({ 1024, EBufferUsage::CopySource, EBufferMemory::CpuWrite });
			Result<BufferHandle> readback =
				device->CreateBuffer({ 512, EBufferUsage::CopyDestination, EBufferMemory::CpuRead });
			Result<BufferHandle> small = device->CreateBuffer({ 511, EBufferUsage::CopyDestination, EBufferMemory::CpuRead });
			ASSERT_TRUE(upload && readback && small);
			Result<Span<Byte>> mapped = device->MapBuffer(upload.Value(), 512, 512, 0);
			ASSERT_TRUE(mapped);

			for (uint32 row = 0; row < 2; ++row)
			{
				for (uint32 index = 0; index < 12; ++index)
				{
					mapped.Value()[row * 256 + index] = static_cast<Byte>(row * 12 + index + 1);
				}
			}

			Result<CommandListHandle> list = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(list);
			EXPECT_DEATH(
				(void)((device->CopyBufferToTexture(list.Value(), { upload.Value(), texture.Value(), subresource, 512 }))),
				"Rendering state, usage, mapping, or buffer alignment is invalid.");
			ASSERT_TRUE(device->UnmapBuffer(upload.Value()));
			EXPECT_DEATH((void)((device->CopyBufferToTexture(list.Value(), { upload.Value(), texture.Value(), subresource, 1 }))), "Rendering state, usage, mapping, or buffer alignment is invalid.");
			EXPECT_DEATH((void)((device->CopyTextureToBuffer(list.Value(), { small.Value(), texture.Value(), subresource, 0 }))), "The texture copy footprint must fit in the buffer.");
			ASSERT_TRUE(device->CopyBufferToTexture(list.Value(), { upload.Value(), texture.Value(), subresource, 512 }));
			ASSERT_TRUE(device->CopyTextureToBuffer(list.Value(), { readback.Value(), texture.Value(), subresource, 0 }));
			ASSERT_FALSE(device->DestroyTexture(texture.Value()));
			SubmitOutcome submitted = device->Submit(list.Value());
			ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
			ASSERT_TRUE(device->WaitForSubmission(submitted.Token, 0));
			Result<Span<Byte>> result = device->MapBuffer(readback.Value(), 0, 512, 0);
			ASSERT_TRUE(result);

			for (uint32 row = 0; row < 2; ++row)
			{
				for (uint32 index = 0; index < 12; ++index)
				{
					ASSERT_EQ(result.Value()[row * 256 + index], static_cast<Byte>(row * 12 + index + 1));
				}
			}

			ASSERT_TRUE(device->UnmapBuffer(readback.Value()));
			Result<StagingTextureHandle> stagingWrite = device->CreateStagingTexture(desc, EBufferMemory::CpuWrite);
			Result<StagingTextureHandle> stagingRead = device->CreateStagingTexture(desc, EBufferMemory::CpuRead);
			ASSERT_TRUE(stagingWrite && stagingRead);
			Result<StagingTextureMapping> stagingMap = device->MapStagingTexture(stagingWrite.Value(), subresource, 0);
			ASSERT_TRUE(stagingMap && stagingMap.Value().Footprint.RowPitchBytes == 256);

			for (uint32 row = 0; row < 2; ++row)
			{
				for (uint32 index = 0; index < 12; ++index)
				{
					stagingMap.Value().Bytes[row * 256 + index] = static_cast<Byte>(60 + row * 12 + index);
				}
			}

			ASSERT_TRUE(device->UnmapStagingTexture(stagingWrite.Value()));
			Result<CommandListHandle> stagingList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(stagingList);
			ASSERT_TRUE(device->CopyStagingTextureToTexture(stagingList.Value(), stagingWrite.Value(), subresource, texture.Value(), subresource));
			ASSERT_TRUE(device->CopyTextureToStagingTexture(stagingList.Value(), texture.Value(), subresource, stagingRead.Value(), subresource));
			ASSERT_FALSE(device->DestroyStagingTexture(stagingWrite.Value()));
			SubmitOutcome stagingSubmitted = device->Submit(stagingList.Value());
			ASSERT_TRUE(stagingSubmitted.Status == ESubmitStatus::Submitted && device->WaitForSubmission(stagingSubmitted.Token, 0));
			Result<StagingTextureMapping> staged = device->MapStagingTexture(stagingRead.Value(), subresource, 0);
			ASSERT_TRUE(staged);

			for (uint32 row = 0; row < 2; ++row)
			{
				for (uint32 index = 0; index < 12; ++index)
				{
					ASSERT_EQ(staged.Value().Bytes[row * 256 + index], static_cast<Byte>(60 + row * 12 + index));
				}
			}

			ASSERT_TRUE(device->UnmapStagingTexture(stagingRead.Value()));
			ASSERT_TRUE(device->DestroyStagingTexture(stagingWrite.Value()));
			ASSERT_TRUE(device->DestroyStagingTexture(stagingRead.Value()));
			ASSERT_TRUE(device->DestroyBuffer(upload.Value()));
			ASSERT_TRUE(device->DestroyBuffer(readback.Value()));
			ASSERT_TRUE(device->DestroyBuffer(small.Value()));
			ASSERT_TRUE(device->DestroyTexture(texture.Value()));
			const TextureDesc blockDesc{ 7, 5, EFormat::BC1_UNORM, ETextureUsage::CopySource | ETextureUsage::CopyDestination };
			Result<TextureHandle> blockTexture = device->CreateTexture(blockDesc);
			ASSERT_TRUE(blockTexture);
			Result<TextureBufferFootprint> blockFootprint = device->GetTextureBufferFootprint(blockTexture.Value(), {});
			ASSERT_TRUE(blockFootprint && blockFootprint.Value().RowPitchBytes == 256 && blockFootprint.Value().RowCount == 2 && blockFootprint.Value().ByteCount == 512);
			Result<BufferHandle> blockUpload = device->CreateBuffer({ 512, EBufferUsage::CopySource, EBufferMemory::CpuWrite });
			Result<BufferHandle> blockReadback =
				device->CreateBuffer({ 512, EBufferUsage::CopyDestination, EBufferMemory::CpuRead });
			ASSERT_TRUE(blockUpload && blockReadback);
			Result<Span<Byte>> blockMap = device->MapBuffer(blockUpload.Value(), 0, 512, 0);
			ASSERT_TRUE(blockMap);

			for (uint32 row = 0; row < 2; ++row)
			{
				for (uint32 index = 0; index < 16; ++index)
				{
					blockMap.Value()[row * 256 + index] = static_cast<Byte>(row * 16 + index + 1);
				}
			}

			ASSERT_TRUE(device->UnmapBuffer(blockUpload.Value()));
			Result<CommandListHandle> blockList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(blockList);
			ASSERT_TRUE(device->CopyBufferToTexture(blockList.Value(), { blockUpload.Value(), blockTexture.Value() }));
			ASSERT_TRUE(device->CopyTextureToBuffer(blockList.Value(), { blockReadback.Value(), blockTexture.Value() }));
			SubmitOutcome blockSubmitted = device->Submit(blockList.Value());
			ASSERT_TRUE(blockSubmitted.Status == ESubmitStatus::Submitted && device->WaitForSubmission(blockSubmitted.Token, 0));
			Result<Span<Byte>> blockResult = device->MapBuffer(blockReadback.Value(), 0, 512, 0);
			ASSERT_TRUE(blockResult);

			for (uint32 row = 0; row < 2; ++row)
			{
				for (uint32 index = 0; index < 16; ++index)
				{
					ASSERT_EQ(blockResult.Value()[row * 256 + index], static_cast<Byte>(row * 16 + index + 1));
				}
			}

			ASSERT_TRUE(device->UnmapBuffer(blockReadback.Value()));
			ASSERT_TRUE(device->DestroyBuffer(blockUpload.Value()));
			ASSERT_TRUE(device->DestroyBuffer(blockReadback.Value()));
			ASSERT_TRUE(device->DestroyTexture(blockTexture.Value()));
			const TextureDesc cubeDesc{ 1, 1, EFormat::RGBA8_UNORM, ETextureUsage::CopySource | ETextureUsage::CopyDestination, 1, 6, 1, ETextureDimension::Cube };
			Result<TextureHandle> cube = device->CreateTexture(cubeDesc);
			Result<StagingTextureHandle> cubeWrite = device->CreateStagingTexture(cubeDesc, EBufferMemory::CpuWrite);
			Result<StagingTextureHandle> cubeRead = device->CreateStagingTexture(cubeDesc, EBufferMemory::CpuRead);
			ASSERT_TRUE(cube && cubeWrite && cubeRead);
			Result<StagingTextureMapping> cubeMap = device->MapStagingTexture(cubeWrite.Value(), { 0, 4 }, 0);
			ASSERT_TRUE(cubeMap && cubeMap.Value().Bytes.Size() == 256);
			cubeMap.Value().Bytes[0] = Byte{ 84 };
			ASSERT_TRUE(device->UnmapStagingTexture(cubeWrite.Value()));
			Result<CommandListHandle> cubeList = device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(cubeList);
			ASSERT_TRUE(device->CopyStagingTextureToTexture(cubeList.Value(), cubeWrite.Value(), { 0, 4 }, cube.Value(), { 0, 4 }));
			ASSERT_TRUE(device->CopyTextureToStagingTexture(cubeList.Value(), cube.Value(), { 0, 4 }, cubeRead.Value(), { 0, 4 }));
			SubmitOutcome cubeSubmitted = device->Submit(cubeList.Value());
			ASSERT_TRUE(cubeSubmitted.Status == ESubmitStatus::Submitted && device->WaitForSubmission(cubeSubmitted.Token, 0));
			Result<StagingTextureMapping> cubeResult = device->MapStagingTexture(cubeRead.Value(), { 0, 4 }, 0);
			ASSERT_TRUE(cubeResult && cubeResult.Value().Bytes[0] == Byte{ 84 });
			ASSERT_TRUE(device->UnmapStagingTexture(cubeRead.Value()));
			ASSERT_TRUE(device->DestroyStagingTexture(cubeWrite.Value()));
			ASSERT_TRUE(device->DestroyStagingTexture(cubeRead.Value()));
			ASSERT_TRUE(device->DestroyTexture(cube.Value()));
		}

		ASSERT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}
}
