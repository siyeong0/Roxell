#include "GpuDeviceTest.h"

namespace rox::rhi
{
	using namespace core;

	TEST_F(GpuDeviceTest, TextureViewsRetainTheirTextureUntilReleased)
	{
		const Result<TextureHandle> texture = m_Device->CreateTexture({ 4, 4, EFormat::RGBA8_UNORM, ETextureUsage::ShaderRead });
		ASSERT_TRUE(texture);
		const Result<TextureViewHandle> first = m_Device->CreateTextureView({ texture.Value() });
		const Result<TextureViewHandle> second = m_Device->CreateTextureView({ texture.Value() });
		ASSERT_TRUE(first);
		ASSERT_TRUE(second);
		const Result<void> retained = m_Device->DestroyTexture(texture.Value());
		ASSERT_FALSE(retained);
		EXPECT_EQ(retained.GetError().Code, EErrorCode::Conflict);
		ASSERT_TRUE(m_Device->DestroyTextureView(first.Value()));
		EXPECT_FALSE(m_Device->DestroyTexture(texture.Value()));
		ASSERT_TRUE(m_Device->DestroyTextureView(second.Value()));
		EXPECT_TRUE(m_Device->DestroyTexture(texture.Value()));
	}

	TEST_F(GpuDeviceTest, PlacedBuffersRetainTheHeapAndReleaseTheirRanges)
	{
		const BufferDesc desc{ 256, EBufferUsage::CopySource | EBufferUsage::CopyDestination };
		const Result<MemoryRequirements> requirements = m_Device->GetBufferMemoryRequirements(desc);
		ASSERT_TRUE(requirements);
		if (requirements.Value().bDedicatedRequired)
		{
			GTEST_SKIP() << "The selected buffer requires dedicated allocation.";
		}
		const uint64 alignment = requirements.Value().AlignmentBytes;
		ASSERT_GT(alignment, 0u);
		const uint64 stride = (requirements.Value().ByteCount + alignment - 1) / alignment * alignment;
		const uint64 capacity = (stride * 2 + 65535) / 65536 * 65536;
		const Result<HeapHandle> heap = m_Device->CreateHeap({ capacity });
		ASSERT_TRUE(heap);
		const Result<BufferHandle> first = m_Device->CreatePlacedBuffer(desc, heap.Value(), 0);
		const Result<BufferHandle> second = m_Device->CreatePlacedBuffer(desc, heap.Value(), stride);
		ASSERT_TRUE(first);
		ASSERT_TRUE(second);
		const Result<void> retained = m_Device->TryDestroyHeap(heap.Value());
		ASSERT_FALSE(retained);
		EXPECT_EQ(retained.GetError().Code, EErrorCode::Conflict);
		const Result<uint64> committed = m_Device->GetHeapCommittedBytes(heap.Value());
		ASSERT_TRUE(committed);
		EXPECT_GT(committed.Value(), 0u);
		ASSERT_TRUE(m_Device->DestroyBuffer(first.Value()));
		EXPECT_FALSE(m_Device->TryDestroyHeap(heap.Value()));
		ASSERT_TRUE(m_Device->DestroyBuffer(second.Value()));
		EXPECT_TRUE(m_Device->TryDestroyHeap(heap.Value()));
	}

	TEST_F(GpuDeviceTest, IntegerAttachmentClearPreservesValuesBeyondFloatPrecision)
	{
		const Result<EFormatSupport> support = m_Device->QueryFormatSupport(EFormat::R32_UINT);
		ASSERT_TRUE(support);
		if (!HasFormatSupport(support.Value(), EFormatSupport::RenderTarget))
		{
			GTEST_SKIP() << "R32_UINT render targets are unsupported.";
		}
		const Result<TextureHandle> texture = m_Device->CreateTexture({ 2, 2, EFormat::R32_UINT, ETextureUsage::RenderTarget | ETextureUsage::CopySource });
		ASSERT_TRUE(texture);
		const Result<FramebufferHandle> framebuffer = m_Device->CreateFramebuffer({ { &texture.Value(), 1 }, {} });
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Graphics);
		ASSERT_TRUE(framebuffer);
		ASSERT_TRUE(list);
		const EAttachmentLoad load = EAttachmentLoad::Clear;
		const ClearColor clear = ClearColor::Uint(16777217u, 0, 0, 0);
		RenderPassDesc pass;
		pass.Layout.Framebuffer = framebuffer.Value();
		pass.Layout.ColorLoads = { &load, 1 };
		pass.ClearValues.Colors = { &clear, 1 };
		ASSERT_TRUE(m_Device->BeginRendering(list.Value(), pass));
		ASSERT_TRUE(m_Device->EndRendering(list.Value()));
		const SubmitOutcome submitted = m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		uint32 values[4]{};
		ASSERT_TRUE(m_Device->ReadTextureBlocking(texture.Value(), { reinterpret_cast<Byte*>(values), sizeof(values) }, submitted.Token, 10000));
		for (const uint32 value : values)
		{
			EXPECT_EQ(value, 16777217u);
		}
	}

	TEST_F(GpuDeviceTest, DepthOnlyPassClearsEveryPixelWithoutColorAttachments)
	{
		const Result<EFormatSupport> support = m_Device->QueryFormatSupport(EFormat::D32);
		ASSERT_TRUE(support);
		if (!HasFormatSupport(support.Value(), EFormatSupport::DepthStencil))
		{
			GTEST_SKIP() << "D32 depth attachments are unsupported.";
		}
		const Result<TextureHandle> depth = m_Device->CreateTexture({ 2, 2, EFormat::D32, ETextureUsage::DepthStencil | ETextureUsage::CopySource });
		ASSERT_TRUE(depth);
		const Result<FramebufferHandle> framebuffer = m_Device->CreateFramebuffer({ {}, depth.Value() });
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Graphics);
		ASSERT_TRUE(framebuffer);
		ASSERT_TRUE(list);
		RenderPassDesc pass;
		pass.Layout.Framebuffer = framebuffer.Value();
		pass.Layout.DepthLoad = EAttachmentLoad::Clear;
		pass.ClearValues.Depth = 0.25f;
		ASSERT_TRUE(m_Device->BeginRendering(list.Value(), pass));
		ASSERT_TRUE(m_Device->EndRendering(list.Value()));
		const SubmitOutcome submitted = m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		float32 values[4]{};
		ASSERT_TRUE(m_Device->ReadTextureBlocking(depth.Value(), { reinterpret_cast<Byte*>(values), sizeof(values) }, submitted.Token, 10000));
		for (const float32 value : values)
		{
			EXPECT_FLOAT_EQ(value, 0.25f);
		}
	}

	TEST_F(GpuDeviceTest, MultisampleResolvePreservesAUniformClearColor)
	{
		TextureDesc sourceDesc{ 4, 4, EFormat::RGBA8_UNORM, ETextureUsage::RenderTarget | ETextureUsage::CopySource };
		const Result<uint32> samples = m_Device->QueryTextureSampleCounts(sourceDesc);
		ASSERT_TRUE(samples);
		if ((samples.Value() & 4u) == 0)
		{
			GTEST_SKIP() << "Four-sample color attachments are unsupported.";
		}
		sourceDesc.SampleCount = 4;
		const Result<bool> support = m_Device->QueryTextureResolveSupport(sourceDesc, ETextureResolveMode::Average);
		ASSERT_TRUE(support);
		if (!support.Value())
		{
			GTEST_SKIP() << "Average color resolve is unsupported.";
		}
		const Result<TextureHandle> source = m_Device->CreateTexture(sourceDesc);
		const Result<TextureHandle> destination = m_Device->CreateTexture({ 4, 4, EFormat::RGBA8_UNORM, ETextureUsage::CopySource | ETextureUsage::CopyDestination });
		ASSERT_TRUE(source);
		ASSERT_TRUE(destination);
		const Result<FramebufferHandle> framebuffer = m_Device->CreateFramebuffer({ { &source.Value(), 1 }, {} });
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Graphics);
		ASSERT_TRUE(framebuffer);
		ASSERT_TRUE(list);
		const EAttachmentLoad load = EAttachmentLoad::Clear;
		const ClearColor clear = ClearColor::Float(0, 0, 1, 1);
		RenderPassDesc pass;
		pass.Layout.Framebuffer = framebuffer.Value();
		pass.Layout.ColorLoads = { &load, 1 };
		pass.ClearValues.Colors = { &clear, 1 };
		ASSERT_TRUE(m_Device->BeginRendering(list.Value(), pass));
		ASSERT_TRUE(m_Device->EndRendering(list.Value()));
		ASSERT_TRUE(m_Device->ResolveTexture(list.Value(), { source.Value(), destination.Value() }));
		const SubmitOutcome submitted = m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		Byte pixels[64]{};
		ASSERT_TRUE(m_Device->ReadTextureBlocking(destination.Value(), pixels, submitted.Token, 10000));
		for (uint32 index = 0; index < sizeof(pixels); ++index)
		{
			EXPECT_EQ(pixels[index], index % 4 < 2 ? Byte{ 0 } : Byte{ 255 });
		}
	}

	TEST_F(GpuDeviceTest, BindingSetsCopyEntriesAndRetainExactlyTheirResources)
	{
		const BindingRangeDesc range{ EDescriptorType::StorageBuffer, 0, 2, EFormat::UNKNOWN, EShaderVisibilityFlags::Compute };
		const Result<BindingLayoutHandle> layout = m_Device->CreateBindingLayout({ EBindingLayoutKind::Compute, 0, { &range, 1 } });
		ASSERT_TRUE(layout);
		const Result<BufferHandle> first = m_Device->CreateBuffer({ 16, EBufferUsage::Storage });
		const Result<BufferHandle> second = m_Device->CreateBuffer({ 16, EBufferUsage::Storage });
		const Result<BufferHandle> unrelated = m_Device->CreateBuffer({ 16, EBufferUsage::Storage });
		ASSERT_TRUE(first);
		ASSERT_TRUE(second);
		ASSERT_TRUE(unrelated);
		BindingEntry entries[]{ { 0, 0, DescriptorValue::Buffer(first.Value(), { 0, 16 }) }, { 0, 1, DescriptorValue::Buffer(second.Value(), { 0, 16 }) } };
		const Result<BindingSetHandle> set = m_Device->CreateBindingSet({ layout.Value(), entries });
		ASSERT_TRUE(set);
		entries[0].Value = DescriptorValue::Buffer(unrelated.Value(), { 0, 16 });
		EXPECT_TRUE(m_Device->DestroyBuffer(unrelated.Value()));
		EXPECT_FALSE(m_Device->DestroyBuffer(first.Value()));
		EXPECT_FALSE(m_Device->DestroyBuffer(second.Value()));
		EXPECT_FALSE(m_Device->DestroyBindingLayout(layout.Value()));
		ASSERT_TRUE(m_Device->DestroyBindingSet(set.Value()));
		EXPECT_TRUE(m_Device->DestroyBuffer(first.Value()));
		EXPECT_TRUE(m_Device->DestroyBuffer(second.Value()));
		EXPECT_TRUE(m_Device->DestroyBindingLayout(layout.Value()));
	}

	TEST_F(GpuDeviceTest, DescriptorCopiesAndResizePreserveThenReleaseResourceOwnership)
	{
		if (m_Device->GetCapabilities().Limits.MaximumBindlessStorageBuffers < 8)
		{
			GTEST_SKIP() << "Storage buffer descriptor tables are unsupported.";
		}
		const Result<DescriptorTableHandle> source = m_Device->CreateDescriptorTable({ EDescriptorType::StorageBuffer, 4 });
		const Result<DescriptorTableHandle> destination = m_Device->CreateDescriptorTable({ EDescriptorType::StorageBuffer, 4 });
		const Result<BufferHandle> buffer = m_Device->CreateBuffer({ 16, EBufferUsage::Storage });
		ASSERT_TRUE(source);
		ASSERT_TRUE(destination);
		ASSERT_TRUE(buffer);
		ASSERT_TRUE(m_Device->WriteDescriptorTable(source.Value(), { 3, DescriptorValue::Buffer(buffer.Value(), { 0, 16 }) }));
		ASSERT_TRUE(m_Device->CopyDescriptorTableEntries(source.Value(), 3, destination.Value(), 1, 1));
		ASSERT_TRUE(m_Device->ClearDescriptorTableEntry(source.Value(), 3));
		EXPECT_FALSE(m_Device->DestroyBuffer(buffer.Value()));
		ASSERT_TRUE(m_Device->ResizeDescriptorTable(destination.Value(), 8));
		EXPECT_FALSE(m_Device->DestroyBuffer(buffer.Value()));
		ASSERT_TRUE(m_Device->ResizeDescriptorTable(destination.Value(), 1));
		EXPECT_TRUE(m_Device->DestroyBuffer(buffer.Value()));
		EXPECT_TRUE(m_Device->DestroyDescriptorTable(source.Value()));
		EXPECT_TRUE(m_Device->DestroyDescriptorTable(destination.Value()));
	}

	TEST_F(GpuDeviceTest, ExternalTexturesCanBeReacquiredWithoutAllowingOrdinaryAccessWhileReleased)
	{
		const ExternalInteropCapabilities capabilities = m_Device->GetExternalInteropCapabilities();
		if (!capabilities.bTextures || !capabilities.bTimelineFences)
		{
			GTEST_SKIP() << "External texture ownership and timeline fences are unsupported.";
		}
		const Result<TextureHandle> texture = m_Device->CreateExternalTexture({ 4, 4, EFormat::RGBA8_UNORM, ETextureUsage::CopySource | ETextureUsage::CopyDestination });
		const Result<ExternalFenceHandle> fence = m_Device->CreateExternalFence();
		ASSERT_TRUE(texture);
		ASSERT_TRUE(fence);
		for (uint64 value = 1; value <= 2; ++value)
		{
			const Result<SubmissionToken> released = m_Device->ReleaseExternalTexture(texture.Value(), EQueue::Graphics, fence.Value(), value);
			ASSERT_TRUE(released);
			ASSERT_TRUE(m_Device->WaitForSubmission(released.Value(), 10000));
			EXPECT_DEATH((void)m_Device->GetNativeTexture(texture.Value()), "permitted resource ownership");
			const Result<SubmissionToken> acquired = m_Device->AcquireExternalTexture(texture.Value(), EQueue::Graphics, fence.Value(), value);
			ASSERT_TRUE(acquired);
			ASSERT_TRUE(m_Device->WaitForSubmission(acquired.Value(), 10000));
			EXPECT_TRUE(m_Device->GetNativeTexture(texture.Value()));
		}
		EXPECT_TRUE(m_Device->DestroyTexture(texture.Value()));
		EXPECT_TRUE(m_Device->DestroyExternalFence(fence.Value()));
	}

}
