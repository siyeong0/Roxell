#include "GpuDeviceTest.h"
#include <cstring>

namespace rox::rhi
{
	using namespace core;

	class GpuComputeTest : public GpuDeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(GpuComputeTest, OrdinaryPreparedAndIndirectDispatchProduceExactBufferValues)
	{
		if (!m_Device->GetCapabilities().Features.bCompute)
		{
			GTEST_SKIP() << "Compute is unsupported.";
		}
		const bool bPrepared = (GetParam() & 1u) != 0;
		const bool bIndirect = (GetParam() & 2u) != 0;
		constexpr uint32 GROUPS = 17;
		const char* source = m_Device->GetBackend() == EBackend::Vulkan
			? "struct Params { uint Add; }; [[vk::push_constant]] ConstantBuffer<Params> Push; "
			  "RWByteAddressBuffer Output : register(u0); "
			  "[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output.Store(id.x * 4, id.x * 3 + Push.Add); }"
			: "struct Params { uint Add; }; ConstantBuffer<Params> Push : register(b0, space1); RWByteAddressBuffer Output : register(u0); "
			  "[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output.Store(id.x * 4, id.x * 3 + Push.Add); }";
		Array<Byte> shader(m_Memory);
		ASSERT_TRUE(compileShader(source, L"main", L"cs_6_0", shader));
		const BindingRangeDesc range{ EDescriptorType::StorageBuffer, 0, 1, EFormat::UNKNOWN, EShaderVisibilityFlags::Compute };
		const Result<BindingLayoutHandle> layout = m_Device->CreateBindingLayout({ EBindingLayoutKind::Compute, 0, { &range, 1 } });
		ASSERT_TRUE(layout);
		ComputePipelineDesc pipelineDesc;
		pipelineDesc.Compute.Source = ShaderInput::Code(shader, "main");
		pipelineDesc.Layouts = { &layout.Value(), 1 };
		pipelineDesc.PushConstantBytes = 4;
		const Result<ComputePipelineHandle> pipeline = m_Device->CreateComputePipeline(pipelineDesc);
		ASSERT_TRUE(pipeline);
		const Result<BufferHandle> output = m_Device->CreateBuffer({ (GROUPS + 1) * 4, EBufferUsage::Storage | EBufferUsage::CopySource | EBufferUsage::CopyDestination });
		ASSERT_TRUE(output);
		Byte initial[(GROUPS + 1) * 4];
		std::memset(initial, 0xa5, sizeof(initial));
		ASSERT_TRUE(m_Device->UploadBuffer(output.Value(), 0, initial));
		const BindingEntry entry{ 0, 0, DescriptorValue::Buffer(output.Value(), { 0, sizeof(initial) }) };
		const Result<BindingSetHandle> set = m_Device->CreateBindingSet({ layout.Value(), { &entry, 1 } });
		ASSERT_TRUE(set);
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Compute);
		ASSERT_TRUE(list);
		BufferHandle arguments;
		if (bIndirect)
		{
			const Result<BufferHandle> buffer = m_Device->CreateBuffer({ 12, EBufferUsage::Indirect | EBufferUsage::CopyDestination });
			ASSERT_TRUE(buffer);
			arguments = buffer.Value();
			const IndirectDispatchArguments values{ GROUPS, 1, 1 };
			ASSERT_TRUE(m_Device->UploadBuffer(arguments, 0, { reinterpret_cast<const Byte*>(&values), sizeof(values) }));
		}
		ComputeDispatch dispatch;
		dispatch.GroupsX = GROUPS;
		const uint32 add = 7;
		dispatch.PushConstants = { reinterpret_cast<const Byte*>(&add), sizeof(add) };
		if (bPrepared)
		{
			PreparedBindingDesc binding = PreparedBindingDesc::Compute(pipeline.Value());
			binding.BindingSets = { &set.Value(), 1 };
			const PreparedBufferAccessDesc accesses[]{ { output.Value(), { 0, sizeof(initial) }, EBufferState::Storage }, { arguments, { 0, 12 }, EBufferState::Indirect } };
			PreparedIndirectDesc indirect;
			indirect.BindingSlot = 0;
			indirect.Arguments = arguments;
			indirect.ArgumentsRange = { 0, 12 };
			CommandAccessPhaseDesc phase;
			phase.Buffers = { accesses, bIndirect ? 2u : 1u };
			phase.Bindings = { &binding, 1 };
			phase.Capacity.Dispatches = 1;
			if (bIndirect)
			{
				phase.Indirects = { &indirect, 1 };
			}
			const CommandListPreparationDesc preparation{ list.Value(), { &phase, 1 } };
			ASSERT_TRUE(m_Device->PrepareCommandLists({ &preparation, 1 }));
			ASSERT_TRUE(m_Device->BeginPreparedRecording({ &list.Value(), 1 }));
			m_bPreparedRecording = true;
			ASSERT_TRUE(m_Device->BeginPreparedAccessPhase(list.Value(), 0));
			if (bIndirect)
			{
				ASSERT_TRUE(m_Device->DispatchComputeIndirect(list.Value(), m_Device->GetPreparedIndirect(list.Value(), 0, 0), dispatch.PushConstants));
			}
			else
			{
				ASSERT_TRUE(m_Device->DispatchCompute(list.Value(), m_Device->GetPreparedBindings(list.Value(), 0, 0), dispatch));
			}
			ASSERT_TRUE(m_Device->FinishPreparedCommandList(list.Value()));
			const Result<ParallelRecordingStatistics> ended = m_Device->EndPreparedRecording();
			m_bPreparedRecording = false;
			ASSERT_TRUE(ended);
		}
		else if (bIndirect)
		{
			ComputeDispatchIndirect indirect;
			indirect.Pipeline = pipeline.Value();
			indirect.Arguments = arguments;
			indirect.Bindings.Sets = { &set.Value(), 1 };
			indirect.PushConstants = dispatch.PushConstants;
			ASSERT_TRUE(m_Device->DispatchComputeIndirect(list.Value(), indirect));
		}
		else
		{
			ComputeBindings bindings;
			bindings.Pipeline = pipeline.Value();
			bindings.Resources.Sets = { &set.Value(), 1 };
			ComputeDispatch invalid = dispatch;
			invalid.PushConstants = { reinterpret_cast<const Byte*>(&add), 3 };
			EXPECT_DEATH((void)m_Device->DispatchCompute(list.Value(), bindings, invalid), "Compute dispatch bindings");
			invalid = dispatch;
			invalid.GroupsX = 0;
			EXPECT_DEATH((void)m_Device->DispatchCompute(list.Value(), bindings, invalid), "Compute dispatch bindings");
			ASSERT_TRUE(m_Device->DispatchCompute(list.Value(), bindings, dispatch));
		}
		const SubmitOutcome submitted = bPrepared ? m_Device->SubmitPrepared(list.Value()) : m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		uint32 values[GROUPS + 1]{};
		ASSERT_TRUE(m_Device->ReadBufferBlocking(output.Value(), 0, { reinterpret_cast<Byte*>(values), sizeof(values) }, submitted.Token, 10000));
		for (uint32 index = 0; index < GROUPS; ++index)
		{
			SCOPED_TRACE(index);
			EXPECT_EQ(values[index], index * 3 + 7);
		}
		EXPECT_EQ(values[GROUPS], 0xa5a5a5a5u);
	}

	INSTANTIATE_TEST_SUITE_P(CommandPaths, GpuComputeTest, testing::Range(0u, 4u));
}
