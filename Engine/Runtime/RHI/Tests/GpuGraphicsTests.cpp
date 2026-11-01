#include "GpuDeviceTest.h"

namespace rox::rhi
{
	using namespace core;

	class GpuGraphicsTest : public GpuDeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(GpuGraphicsTest, OrdinaryPreparedDrawAndIndexedDrawProduceTheSamePixels)
	{
		if (!m_Device->GetCapabilities().Features.bGraphics)
		{
			GTEST_SKIP() << "Graphics is unsupported.";
		}
		const bool bPrepared = (GetParam() & 1u) != 0;
		const bool bIndexed = (GetParam() & 2u) != 0;
		const bool bIndex32 = (GetParam() & 4u) != 0;
		const EFormat indexFormat = bIndex32 ? EFormat::R32_UINT : EFormat::R16_UINT;
		const uint32 indexBytes = bIndex32 ? 20u : 12u;
		constexpr char VERTEX[] = "float4 main(float2 position : POSITION) : SV_Position { return float4(position,0,1); }";
		constexpr char PIXEL[] = "float4 main() : SV_Target0 { return float4(0,1,0,1); }";
		Array<Byte> vertex(m_Memory);
		Array<Byte> pixel(m_Memory);
		ASSERT_TRUE(compileShader(VERTEX, L"main", L"vs_6_0", vertex));
		ASSERT_TRUE(compileShader(PIXEL, L"main", L"ps_6_0", pixel));
		const float32 positions[]{ 99, 99, -1, -1, -1, 3, 3, -1 };
		const Result<BufferHandle> vertices = m_Device->CreateBuffer({ sizeof(positions), EBufferUsage::Vertex | EBufferUsage::CopyDestination });
		ASSERT_TRUE(vertices);
		ASSERT_TRUE(m_Device->UploadBuffer(vertices.Value(), 0, { reinterpret_cast<const Byte*>(positions), sizeof(positions) }));
		const VertexAttributeDesc attribute{ 0, 0, EFormat::RG32_FLOAT, 0, "POSITION", 0 };
		GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.VertexStrideBytes = 8;
		pipelineDesc.VertexAttributes = { &attribute, 1 };
		pipelineDesc.Vertex.Source = ShaderInput::Code(vertex, "main");
		pipelineDesc.Pixel.Source = ShaderInput::Code(pixel, "main");
		const Result<GraphicsPipelineHandle> pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
		ASSERT_TRUE(pipeline);
		const Result<TextureHandle> target = m_Device->CreateTexture({ 8, 8, EFormat::RGBA8_UNORM, ETextureUsage::RenderTarget | ETextureUsage::CopySource });
		ASSERT_TRUE(target);
		const Result<FramebufferHandle> framebuffer = m_Device->CreateFramebuffer({ { &target.Value(), 1 }, {} });
		ASSERT_TRUE(framebuffer);
		BufferHandle indices;
		if (bIndexed)
		{
			const Result<BufferHandle> buffer = m_Device->CreateBuffer({ indexBytes, EBufferUsage::Index | EBufferUsage::CopyDestination });
			ASSERT_TRUE(buffer);
			indices = buffer.Value();
			const uint16 values16[]{ 88, 88, 99, 6, 7, 8 };
			const uint32 values32[]{ 88, 99, 6, 7, 8 };
			const Byte* values = bIndex32 ? reinterpret_cast<const Byte*>(values32) : reinterpret_cast<const Byte*>(values16);
			ASSERT_TRUE(m_Device->UploadBuffer(indices, 0, { values, indexBytes }));
		}
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Graphics);
		ASSERT_TRUE(list);
		const EAttachmentLoad load = EAttachmentLoad::Clear;
		const ClearColor clear = ClearColor::Float(1, 0, 0, 1);
		RenderPassDesc pass;
		pass.Layout.Framebuffer = framebuffer.Value();
		pass.Layout.ColorLoads = { &load, 1 };
		pass.ClearValues.Colors = { &clear, 1 };
		DrawArguments draw;
		draw.VertexCount = 3;
		DrawIndexedArguments indexed;
		indexed.IndexCount = 3;
		indexed.FirstIndex = 1;
		indexed.BaseVertex = -6;
		if (bPrepared)
		{
			PreparedBindingDesc binding = PreparedBindingDesc::Graphics(pipeline.Value());
			const PreparedVertexBufferBinding vertexBinding{ vertices.Value(), { 8, sizeof(positions) - 8 } };
			binding.VertexBuffers = { &vertexBinding, 1 };
			binding.IndexBuffer = indices;
			binding.IndexRange = { bIndexed ? 4u : 0u, bIndexed ? indexBytes - 4u : 0u };
			binding.IndexFormat = indexFormat;
			const PreparedTextureAccessDesc textureAccess{ target.Value(), 0, 1, 0, 1, ETextureAspect::Color, ETextureState::RenderTarget };
			const PreparedBufferAccessDesc bufferAccesses[]{
				{ vertices.Value(), { 8, sizeof(positions) - 8 }, EBufferState::Vertex },
				{ indices, { 4, indexBytes - 4u }, EBufferState::Index }
			};
			CommandAccessPhaseDesc phase;
			phase.Textures = { &textureAccess, 1 };
			phase.Bindings = { &binding, 1 };
			phase.Attachments = { &pass.Layout, 1 };
			phase.Capacity.Draws = 1;
			phase.Capacity.RenderPasses = 1;
			phase.Capacity.ViewportChanges = 1;
			phase.Capacity.ScissorChanges = 1;
			phase.Buffers = { bufferAccesses, bIndexed ? 2u : 1u };
			const CommandListPreparationDesc preparation{ list.Value(), { &phase, 1 } };
			ASSERT_TRUE(m_Device->PrepareCommandLists({ &preparation, 1 }));
			ASSERT_TRUE(m_Device->BeginPreparedRecording({ &list.Value(), 1 }));
			m_bPreparedRecording = true;
			ASSERT_TRUE(m_Device->BeginPreparedAccessPhase(list.Value(), 0));
			ASSERT_TRUE(m_Device->BeginRendering(list.Value(), m_Device->GetPreparedAttachment(list.Value(), 0, 0), pass.ClearValues));
			ASSERT_TRUE(m_Device->SetViewport(list.Value(), { 0, 0, 8, 8 }));
			ASSERT_TRUE(m_Device->SetScissor(list.Value(), { 0, 0, 8, 8 }));
			const PreparedBindingRef bindings = m_Device->GetPreparedBindings(list.Value(), 0, 0);
			ASSERT_TRUE(bIndexed ? m_Device->DrawIndexed(list.Value(), bindings, indexed) : m_Device->Draw(list.Value(), bindings, draw));
			ASSERT_TRUE(m_Device->EndRendering(list.Value()));
			ASSERT_TRUE(m_Device->FinishPreparedCommandList(list.Value()));
			const Result<ParallelRecordingStatistics> ended = m_Device->EndPreparedRecording();
			m_bPreparedRecording = false;
			ASSERT_TRUE(ended);
		}
		else
		{
			GraphicsState state;
			state.Pipeline = pipeline.Value();
			state.VertexBuffer = vertices.Value();
			state.VertexBufferOffsetBytes = 8;
			state.IndexBuffer = indices;
			state.IndexBufferOffsetBytes = bIndexed ? 4u : 0u;
			state.IndexFormat = indexFormat;
			ASSERT_TRUE(m_Device->BeginRendering(list.Value(), pass));
			ASSERT_TRUE(m_Device->SetViewport(list.Value(), { 0, 0, 8, 8 }));
			ASSERT_TRUE(m_Device->SetScissor(list.Value(), { 0, 0, 8, 8 }));
			ASSERT_TRUE(bIndexed ? m_Device->DrawIndexed(list.Value(), state, indexed) : m_Device->Draw(list.Value(), state, draw));
			ASSERT_TRUE(m_Device->EndRendering(list.Value()));
		}
		const SubmitOutcome submitted = bPrepared ? m_Device->SubmitPrepared(list.Value()) : m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		Byte pixels[8 * 8 * 4]{};
		ASSERT_TRUE(m_Device->ReadTextureBlocking(target.Value(), pixels, submitted.Token, 10000));
		for (uint32 index = 0; index < sizeof(pixels); index += 4)
		{
			SCOPED_TRACE(index / 4);
			EXPECT_EQ(pixels[index], Byte{ 0 });
			EXPECT_EQ(pixels[index + 1], Byte{ 255 });
			EXPECT_EQ(pixels[index + 2], Byte{ 0 });
			EXPECT_EQ(pixels[index + 3], Byte{ 255 });
		}
	}

	INSTANTIATE_TEST_SUITE_P(CommandPaths, GpuGraphicsTest, testing::Values(0u, 1u, 2u, 3u, 6u, 7u));
}
