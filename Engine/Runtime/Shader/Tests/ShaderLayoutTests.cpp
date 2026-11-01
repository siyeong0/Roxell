#include "ShaderTestData.h"

namespace rox::shader::test
{
	using namespace core;

	TEST_F(ShaderTest, IdenticalLayoutsReportStructuralIdentityButNoSemanticProof)
	{
		Result<Shader> asset = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(asset);
		ShaderOptions options(m_Memory);
		Result<ShaderKernelSelection> first = asset.Value().SelectKernel("Simulate", {}, options, m_Memory);
		ASSERT_TRUE(first);
		Result<ShaderKernelSelection> second = asset.Value().SelectKernel("Simulate", {}, options, m_Memory);
		ASSERT_TRUE(second);
		Result<ShaderInterfaceCompatibility> compared = CompareShaderLayouts(first.Value().GetLayout(), second.Value().GetLayout(), m_Memory);
		ASSERT_TRUE(compared);
		EXPECT_EQ(compared.Value().Level, EShaderInterfaceCompatibility::Identical);
		EXPECT_EQ(compared.Value().Semantic, EShaderSemanticCompatibility::Unproven);
		EXPECT_FALSE(compared.Value().bCodeChanged);
		EXPECT_EQ(first.Value().GetLayout().GetPhysicalLayoutHash(), second.Value().GetLayout().GetPhysicalLayoutHash());
	}

	TEST_F(ShaderTest, LayoutRejectsEmptyInvalidDuplicateAndMixedTargetSelections)
	{
		expectError(ShaderLayout::Create({}, m_Memory), EErrorCode::InvalidArgument);
		ShaderEntrySelection invalid;
		expectError(ShaderLayout::Create({&invalid, 1}, m_Memory), EErrorCode::InvalidArgument);
		Result<Shader> asset = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(asset);
		Result<uint32> dxil = asset.Value().FindVariant({"Main", {}});
		Result<uint32> spirv = asset.Value().FindVariant({"Main", profile(EShaderTarget::SpirV)});
		ASSERT_TRUE(dxil);
		ASSERT_TRUE(spirv);
		ShaderEntrySelection entries[2] = {{asset.Value(), dxil.Value()}, {asset.Value(), dxil.Value()}};
		EXPECT_FALSE(ShaderLayout::Create(entries, m_Memory));
		entries[1].VariantIndex = spirv.Value();
		EXPECT_FALSE(ShaderLayout::Create(entries, m_Memory));
		entries[0].VariantIndex = 9999;
		expectError(ShaderLayout::Create({entries, 1}, m_Memory), EErrorCode::InvalidArgument);
	}

	TEST_F(ShaderTest, BindingsExposeExpectedKindsAndStableParameterLookup)
	{
		Result<Shader> asset = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(asset);
		ShaderOptions options(m_Memory);
		Result<ShaderKernelSelection> selected = asset.Value().SelectKernel("Simulate", {}, options, m_Memory);
		ASSERT_TRUE(selected);
		const ShaderLayout& layout = selected.Value().GetLayout();
		uint32 constants = 0;
		uint32 inputs = 0;
		uint32 outputs = 0;
		for (const ShaderProgramBinding& binding : layout.GetBindings())
		{
			Result<uint32> found = layout.FindParameter(binding.ParameterID);
			ASSERT_TRUE(found);
			EXPECT_EQ(layout.GetBindings()[found.Value()].Binding.Name, binding.Binding.Name);
			constants += binding.Binding.Kind == EShaderBindingKind::ConstantBuffer;
			inputs += binding.Binding.Kind == EShaderBindingKind::ReadOnlyBuffer;
			outputs += binding.Binding.Kind == EShaderBindingKind::ReadWriteBuffer;
		}
		EXPECT_EQ(constants, 1u);
		EXPECT_EQ(inputs, 1u);
		EXPECT_EQ(outputs, 1u);
		expectError(layout.FindParameter({}), EErrorCode::NotFound);
	}

	TEST_F(ShaderTest, LayoutCancellationAndAllocationFailurePreserveSelection)
	{
		Result<Shader> asset = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(asset);
		ShaderEntrySelection entry{asset.Value(), 0};
		CancellationSource cancel(m_Memory);
		cancel.RequestCancellation();
		expectError(ShaderLayout::Create({&entry, 1}, m_Memory, cancel.GetToken()), EErrorCode::Cancelled);
		FailingMemoryResource failing(m_Memory, 0);
		expectError(ShaderLayout::Create({&entry, 1}, failing), EErrorCode::ExternalFailure);
		EXPECT_TRUE(ShaderLayout::Create({&entry, 1}, m_Memory));
	}

	TEST_F(ShaderTest, LayoutAndResolvedConstantsRetainTheirOwners)
	{
		ShaderLayout retained;
		ShaderPipelineConstants constants;
		{
			Result<Shader> asset = loadFixture("Compute", m_Memory);
			ASSERT_TRUE(asset);
			ShaderOptions options(m_Memory);
			Result<ShaderKernelSelection> selected = asset.Value().SelectKernel("Simulate", {}, options, m_Memory);
			ASSERT_TRUE(selected);
			retained = selected.Value().GetLayout();
			constants = selected.Value().GetPipelineConstants();
		}
		EXPECT_EQ(retained.GetBindings().Size(), 3u);
		EXPECT_EQ(constants.GetThreadGroupSize()[0], 8u);
		EXPECT_EQ(constants.GetLayout().GetLayoutHash(), retained.GetLayoutHash());
	}

	class LayoutChangeTest : public ShaderTest, public testing::WithParamInterface<int>
	{
	};

	TEST_P(LayoutChangeTest, SeparatesLogicalNamesPhysicalOffsetsAndCodeIdentity)
	{
		Result<Shader> original = loadFixture("Parameters", m_Memory);
		ASSERT_TRUE(original);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(original.Value(), m_Memory);
		ASSERT_TRUE(draft);
		Result<uint32> index = original.Value().FindVariant({"Main", {}});
		ASSERT_TRUE(index);
		ShaderVariant& variant = draft.Value().Variants[index.Value()];
		for (const ShaderBinding& binding : variant.Bindings)
		{
			if (binding.Kind != EShaderBindingKind::ConstantBuffer)
			{
				continue;
			}
			ShaderDataLayout& layout = variant.Layouts[binding.DataLayout];
			if (GetParam() == 0)
			{
				layout.Fields[0].Name.AssignRequired("RenamedTint");
			}
			else
			{
				layout.ByteSize += 16;
				if (layout.ElementStride != 0)
				{
					layout.ElementStride += 16;
				}
				for (ShaderDataField& field : layout.Fields)
				{
					field.ByteOffset += 16;
				}
			}
		}
		Result<Shader> changed = Move(draft).TakeValue().Build(m_Memory);
		ASSERT_TRUE(changed) << changed.GetError().Message.CStr();
		ShaderOptions options(m_Memory);
		Result<ShaderKernelSelection> first = original.Value().SelectKernel("Simulate", {}, options, m_Memory);
		Result<ShaderKernelSelection> second = changed.Value().SelectKernel("Simulate", {}, options, m_Memory);
		ASSERT_TRUE(first);
		ASSERT_TRUE(second);
		const ShaderLayout& left = first.Value().GetLayout();
		const ShaderLayout& right = second.Value().GetLayout();
		EXPECT_EQ(left.GetSelectedCodeHash(), right.GetSelectedCodeHash());
		EXPECT_NE(left.GetLayoutHash(), right.GetLayoutHash());
		Result<ShaderInterfaceCompatibility> compared = CompareShaderLayouts(left, right, m_Memory);
		ASSERT_TRUE(compared);
		EXPECT_EQ(compared.Value().Level, GetParam() == 0 ? EShaderInterfaceCompatibility::BindingCompatible : EShaderInterfaceCompatibility::RepackRequired);
		EXPECT_EQ(left.GetPhysicalLayoutHash() == right.GetPhysicalLayoutHash(), GetParam() == 0);
	}

	INSTANTIATE_TEST_SUITE_P(Changes, LayoutChangeTest, testing::Range(0, 2));
	// These drafts exercise metadata contracts. Their code comes from a real fixture;
	// changing declared stages does not claim that the code executes as those stages.
	namespace
	{
		Result<ShaderBuilder> stageDraft(EShaderStage stage, EShaderTarget target, MemoryResource& memory)
		{
			Result<Shader> source = loadFixture("Compute", memory);
			if (!source)
			{
				return Result<ShaderBuilder>::Failure(source.GetError());
			}
			Result<uint32> index = source.Value().FindVariant({"Main", profile(target)});
			if (!index)
			{
				return Result<ShaderBuilder>::Failure(index.GetError());
			}
			ShaderBuilder draft(memory);
			ShaderEntryPoint entry(memory);
			entry.Name.AssignRequired("Main");
			entry.Stage = stage;
			const bool bGroup = stage == EShaderStage::Compute || stage == EShaderStage::Mesh || stage == EShaderStage::Task;
			if (bGroup)
			{
				entry.ThreadGroupSize[0] = 32;
				entry.ThreadGroupSize[1] = entry.ThreadGroupSize[2] = 1;
			}
			draft.EntryPoints.AddRequired(Move(entry));
			ShaderVariant variant(memory);
			variant.Target = profile(target);
			variant.EmittedEntryPoint.AssignRequired("Main");
			const Array<Byte>& code = source.Value().GetVariants()[index.Value()].Bytecode;
			if (!variant.Bytecode.ResizeUninitialized(code.Size()))
			{
				return Result<ShaderBuilder>::Failure(EErrorCode::ExternalFailure, "Shader.Test", "Allocation failed.");
			}
			CopyBytes(variant.Bytecode.Data(), code.Data(), code.Size());
			variant.Requirements.MinimumWorkgroupInvocations = bGroup ? 32 : 0;
			if (stage == EShaderStage::Mesh || stage == EShaderStage::Task)
			{
				variant.RequiredCapabilities = static_cast<uint64>(EShaderCapability::Mesh);
				variant.Requirements.Features = static_cast<uint64>(EShaderFeature::Mesh);
				if (stage == EShaderStage::Mesh)
				{
					variant.MeshOutput = {EShaderMeshTopology::Triangles, 3, 1};
				}
				else
				{
					variant.Requirements.Features |= static_cast<uint64>(EShaderFeature::Task);
				}
			}
			if (stage == EShaderStage::Miss)
			{
				variant.RequiredCapabilities = static_cast<uint64>(EShaderCapability::RayTracing);
				variant.Requirements.Features = static_cast<uint64>(EShaderFeature::RayTracing);
				variant.bStageIOComplete = true;
				variant.RayInterface.bOutgoingDataComplete = target == EShaderTarget::SpirV;
			}
			draft.Variants.AddRequired(Move(variant));
			return Result<ShaderBuilder>::Success(Move(draft));
		}

		void addPayload(Array<ShaderPayloadType>& types, MemoryResource& memory, EShaderScalar scalar)
		{
			ShaderPayloadType leaf(memory);
			leaf.Scalar = scalar;
			leaf.ScalarWidth = 32;
			types.AddRequired(Move(leaf));
			ShaderPayloadType root(memory);
			root.Kind = EShaderPayloadTypeKind::Structure;
			root.MemberTypes.AddRequired(0);
			types.AddRequired(Move(root));
		}
	}

	class MeshContractTest : public ShaderTest, public testing::WithParamInterface<int>
	{
	};

	TEST_P(MeshContractTest, PreservesRequirementsAndValidatesTaskPayloadComposition)
	{
		Result<ShaderBuilder> mesh = stageDraft(EShaderStage::Mesh, EShaderTarget::SpirV, m_Memory);
		Result<ShaderBuilder> task = stageDraft(EShaderStage::Task, EShaderTarget::SpirV, m_Memory);
		ASSERT_TRUE(mesh);
		ASSERT_TRUE(task);
		if (GetParam() >= 2)
		{
			for (ShaderBuilder* draft : {&mesh.Value(), &task.Value()})
			{
				ShaderTaskPayloadSignature& payload = draft->Variants[0].TaskPayload;
				payload.Proof = EShaderTaskPayloadProof::Structural;
				payload.RootType = 1;
				addPayload(payload.Types, m_Memory, GetParam() == 3 && draft == &task.Value() ? EShaderScalar::UnsignedInteger : EShaderScalar::Float);
			}
		}
		Result<Shader> meshAsset = Move(mesh).TakeValue().Build(m_Memory);
		Result<Shader> taskAsset = Move(task).TakeValue().Build(m_Memory);
		ASSERT_TRUE(meshAsset) << meshAsset.GetError().Message.CStr();
		ASSERT_TRUE(taskAsset) << taskAsset.GetError().Message.CStr();
		ShaderEntrySelection entries[] = {{meshAsset.Value(), 0}, {taskAsset.Value(), 0}};
		Result<ShaderLayout> layout = ShaderLayout::Create({entries, GetParam() == 0 ? 1u : 2u}, m_Memory);
		if (GetParam() == 3)
		{
			EXPECT_FALSE(layout);
			return;
		}
		ASSERT_TRUE(layout) << layout.GetError().Message.CStr();
		Result<ShaderPipelineConstants> resolved = ShaderPipelineConstants::Resolve(layout.Value(), {}, m_Memory);
		ASSERT_TRUE(resolved);
		EXPECT_EQ(resolved.Value().GetFeatureRequirements(), layout.Value().GetFeatureRequirements());
		EXPECT_EQ(resolved.Value().GetFeatureRequirements().MinimumWorkgroupInvocations, 32u);
		EXPECT_FALSE(ShaderLayout::Create({entries + 1, 1}, m_Memory));
		if (GetParam() == 2)
		{
			EXPECT_FALSE(ShaderLayout::Create({entries, 1}, m_Memory));
		}
	}

	INSTANTIATE_TEST_SUITE_P(Metadata, MeshContractTest, testing::Range(0, 4));

	class RayContractTest : public ShaderTest, public testing::WithParamInterface<EShaderTarget>
	{
	};

	TEST_P(RayContractTest, ClonesAndSerializesPayloadAndRejectsInvalidBoundsAndReferences)
	{
		Result<ShaderBuilder> draft = stageDraft(EShaderStage::Miss, GetParam(), m_Memory);
		ASSERT_TRUE(draft);
		ShaderRayInterface& ray = draft.Value().Variants[0].RayInterface;
		ray.DeclaredLimits.MaximumPayloadBytes = 4;
		ShaderRayDataSignature data(m_Memory);
		data.RootType = 1;
		addPayload(data.Types, m_Memory, EShaderScalar::Float);
		ray.Data.AddRequired(Move(data));
		ASSERT_TRUE(draft.Value().Validate()) << draft.Value().Validate().GetError().Message.CStr();
		ray.DeclaredLimits.MaximumPayloadBytes = 3;
		EXPECT_FALSE(draft.Value().Validate());
		ray.DeclaredLimits.MaximumPayloadBytes = 4;
		ray.Data[0].RootType = 99;
		EXPECT_FALSE(draft.Value().Validate());
		ray.Data[0].RootType = 1;
		Result<Shader> asset = Move(draft).TakeValue().Build(m_Memory);
		ASSERT_TRUE(asset);
		Result<ShaderBuilder> copy = ShaderBuilder::FromShader(asset.Value(), m_Memory);
		ASSERT_TRUE(copy);
		EXPECT_EQ(copy.Value().Variants[0].RayInterface.Data[0].Types.Size(), 2u);
		Result<Array<Byte>> bytes = asset.Value().Save(m_Memory);
		ASSERT_TRUE(bytes);
		Result<Shader> loaded = Shader::Load(bytes.Value().View(), m_Memory);
		ASSERT_TRUE(loaded);
		EXPECT_EQ(loaded.Value().GetIdentity(), asset.Value().GetIdentity());
		EXPECT_EQ(loaded.Value().GetVariants()[0].RayInterface.DeclaredLimits.MaximumPayloadBytes, 4u);
	}

	INSTANTIATE_TEST_SUITE_P(Targets, RayContractTest, testing::Values(EShaderTarget::Dxil, EShaderTarget::SpirV));

	TEST_F(ShaderTest, LocalRootRecipeSurvivesCloneAndSerializationAndRejectsBadBinding)
	{
		Result<ShaderBuilder> draft = stageDraft(EShaderStage::Miss, EShaderTarget::Dxil, m_Memory);
		ASSERT_TRUE(draft);
		ShaderVariant& variant = draft.Value().Variants[0];
		ShaderBinding binding(m_Memory);
		binding.Name.AssignRequired("LocalBuffer");
		binding.Kind = EShaderBindingKind::ReadOnlyBuffer;
		binding.Scope = EShaderBindingScope::LocalRecord;
		binding.RegisterNamespace = EShaderRegisterClass::ShaderResource;
		variant.Bindings.AddRequired(Move(binding));
		variant.ResourceUses.AddRequired({0, EShaderResourceAccess::Read});
		variant.LocalRecord.Lowering = EShaderLocalRecordLowering::D3D12LocalRootArguments;
		variant.LocalRecord.Proof = EShaderLocalRecordProof::DeclaredDxilRecipe;
		variant.LocalRecord.ByteSize = 8;
		variant.LocalRecord.Alignment = 8;
		variant.LocalRootArguments.AddRequired({EShaderLocalRootArgumentKind::ShaderResourceAddress, 0, 0, 0});
		ASSERT_TRUE(draft.Value().Validate()) << draft.Value().Validate().GetError().Message.CStr();
		variant.LocalRootArguments[0].BindingIndex = 9;
		EXPECT_FALSE(draft.Value().Validate());
		variant.LocalRootArguments[0].BindingIndex = 0;
		variant.LocalRecord.ByteSize = MAXIMUM_DXIL_LOCAL_ROOT_BYTES + 1;
		EXPECT_FALSE(draft.Value().Validate());
		variant.LocalRecord.ByteSize = 8;
		variant.LocalRecord.Alignment = 8;
		Result<Shader> asset = Move(draft).TakeValue().Build(m_Memory);
		ASSERT_TRUE(asset);
		Result<ShaderBuilder> copy = ShaderBuilder::FromShader(asset.Value(), m_Memory);
		ASSERT_TRUE(copy);
		EXPECT_EQ(copy.Value().Variants[0].LocalRootArguments[0].Kind, EShaderLocalRootArgumentKind::ShaderResourceAddress);
		Result<Array<Byte>> bytes = asset.Value().Save(m_Memory);
		ASSERT_TRUE(bytes);
		Result<Shader> loaded = Shader::Load(bytes.Value().View(), m_Memory);
		ASSERT_TRUE(loaded);
		EXPECT_EQ(loaded.Value().GetVariants()[0].LocalRecord.ByteSize, 8u);
		EXPECT_EQ(loaded.Value().GetVariants()[0].Bindings[0].Scope, EShaderBindingScope::LocalRecord);
	}

	class StageLinkFailureTest : public ShaderTest, public testing::WithParamInterface<int>
	{
	};

	TEST_P(StageLinkFailureTest, RejectsIncompatibleProducerAndConsumer)
	{
		Result<Shader> fixture = loadFixture("Surface", m_Memory);
		ASSERT_TRUE(fixture);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(fixture.Value(), m_Memory);
		ASSERT_TRUE(draft);
		Result<uint32> pixel = fixture.Value().FindVariant({"Pixel", profile(EShaderTarget::SpirV)});
		ASSERT_TRUE(pixel);
		bool bChanged = false;
		for (ShaderStageIO& input : draft.Value().Variants[pixel.Value()].StageInputs)
		{
			if (input.Builtin != EShaderBuiltin::None)
			{
				continue;
			}
			if (GetParam() == 0)
			{
				input.VectorWidth = input.VectorWidth == 2 ? 3 : 2;
			}
			else if (GetParam() == 1)
			{
				input.Location = 31;
			}
			else
			{
				input.Interpolation = EShaderInterpolation::Flat;
			}
			bChanged = true;
			break;
		}
		ASSERT_TRUE(bChanged);
		Result<Shader> asset = Move(draft).TakeValue().Build(m_Memory);
		ASSERT_TRUE(asset) << asset.GetError().Message.CStr();
		ShaderOptions options(m_Memory);
		EXPECT_FALSE(asset.Value().SelectPass("Surface", profile(EShaderTarget::SpirV), options, m_Memory));
	}

	INSTANTIATE_TEST_SUITE_P(Mismatch, StageLinkFailureTest, testing::Range(0, 3));

	class ConstantBoundaryTest : public ShaderTest, public testing::WithParamInterface<int>
	{
	};

	TEST_P(ConstantBoundaryTest, ChecksWorkgroupBoolAndWideScalarBoundaries)
	{
		Result<ShaderBuilder> draft = stageDraft(EShaderStage::Compute, EShaderTarget::SpirV, m_Memory);
		ASSERT_TRUE(draft);
		ShaderSpecializationParameter parameter(m_Memory);
		parameter.Name.AssignRequired("Value");
		parameter.ID = 7;
		const int scenario = GetParam();
		ShaderSpecializationValue value{7, EShaderScalar::UnsignedInteger, 32, 1024};
		if (scenario < 4)
		{
			parameter.WorkgroupAxis = 0;
			value.Bits = scenario == 0 ? 1024 : scenario == 1 ? 1025 : scenario == 2 ? 0 : 65536;
		}
		else if (scenario < 6)
		{
			parameter.Scalar = EShaderScalar::Bool;
			value.Scalar = EShaderScalar::Bool;
			value.Bits = scenario == 4 ? 1 : 2;
		}
		else
		{
			parameter.ScalarWidth = 64;
			value.ScalarWidth = 64;
			const bool bFloat = scenario >= 8;
			parameter.Scalar = bFloat ? EShaderScalar::Float : EShaderScalar::UnsignedInteger;
			value.Scalar = parameter.Scalar;
			const uint64 feature = static_cast<uint64>(bFloat ? EShaderFeature::Float64 : EShaderFeature::Int64);
			draft.Value().Variants[0].RequiredCapabilities |= feature;
			draft.Value().Variants[0].Requirements.Features |= feature;
			if (!bFloat)
			{
				parameter.bHasRange = true;
				parameter.MinimumBits = 0xfffffffffffffffeull;
				parameter.MaximumBits = 0xffffffffffffffffull;
				value.Bits = scenario == 6 ? 0xffffffffffffffffull : 0xfffffffffffffffdull;
			}
			else
			{
				value.Bits = scenario == 8 ? 0x8000000000000000ull : 0x7ff0000000000000ull;
			}
		}
		draft.Value().Variants[0].SpecializationParameters.AddRequired(Move(parameter));
		Result<Shader> asset = Move(draft).TakeValue().Build(m_Memory);
		ASSERT_TRUE(asset) << asset.GetError().Message.CStr();
		ShaderEntrySelection entry{asset.Value(), 0};
		Result<ShaderLayout> layout = ShaderLayout::Create({&entry, 1}, m_Memory);
		ASSERT_TRUE(layout);
		Result<ShaderPipelineConstants> resolved = ShaderPipelineConstants::Resolve(layout.Value(), {&value, 1}, m_Memory);
		if (scenario == 0 || scenario == 4 || scenario == 6 || scenario == 8)
		{
			ASSERT_TRUE(resolved);
			EXPECT_EQ(resolved.Value().GetValues()[0].Bits, value.Bits);
			EXPECT_EQ(resolved.Value().GetFeatureRequirements().MinimumWorkgroupInvocations, scenario == 0 ? 1024u : 32u);
		}
		else
		{
			expectError(resolved, EErrorCode::InvalidArgument);
		}
	}

	INSTANTIATE_TEST_SUITE_P(Boundaries, ConstantBoundaryTest, testing::Range(0, 10));

}
