#include "ShaderTestData.h"

namespace rox::shader::test
{
	using namespace core;

	class ShaderSerializationTest : public ShaderTest, public testing::WithParamInterface<const char*>
	{
	};

	TEST_P(ShaderSerializationTest, SavedRealAssetMatchesGoldenBytesAndIdentity)
	{
		Result<Array<Byte>> golden = readFixture(GetParam(), m_Memory);
		ASSERT_TRUE(golden);
		const Byte header[] = {Byte{'R'}, Byte{'X'}, Byte{'S'}, Byte{'H'}, Byte{'S'}, Byte{'E'}, Byte{'T'}, Byte{'1'}, Byte{1}, Byte{0}, Byte{0}, Byte{0}};
		ASSERT_GE(golden.Value().Size(), sizeof(header));
		expectBytes({golden.Value().Data(), sizeof(header)}, header);
		Result<Shader> loaded = Shader::Load(golden.Value().View(), m_Memory);
		ASSERT_TRUE(loaded);
		Result<Array<Byte>> saved = loaded.Value().Save(m_Memory);
		ASSERT_TRUE(saved);
		expectBytes(saved.Value().View(), golden.Value().View());
		EXPECT_EQ(ComputeSha256(golden.Value().View()), loaded.Value().GetIdentity());
		Result<Shader> second = Shader::Load(saved.Value().View(), m_Memory);
		ASSERT_TRUE(second);
		EXPECT_EQ(second.Value().GetIdentity(), loaded.Value().GetIdentity());
	}

	TEST_P(ShaderSerializationTest, TruncatedHeadersPayloadAndTailFailWithoutPublishing)
	{
		Result<Array<Byte>> bytes = readFixture(GetParam(), m_Memory);
		ASSERT_TRUE(bytes);
		const usize length = bytes.Value().Size();
		for (usize cut = 0; cut < 64; ++cut)
		{
			SCOPED_TRACE(cut);
			Result<Shader> result = Shader::Load({bytes.Value().Data(), cut}, m_Memory);
			ASSERT_FALSE(result);
			EXPECT_STREQ(result.GetError().DiagnosticID.CStr(), "ROX-SHADER-ARTIFACT");
		}
		for (usize cut : {length / 4, length / 2, length - 16, length - 1})
		{
			SCOPED_TRACE(cut);
			EXPECT_FALSE(Shader::Load({bytes.Value().Data(), cut}, m_Memory));
		}
		EXPECT_TRUE(Shader::Load(bytes.Value().View(), m_Memory));
	}

	TEST_P(ShaderSerializationTest, VersionAndLengthCorruptionAndTrailingBytesAreRejected)
	{
		Result<Array<Byte>> bytes = readFixture(GetParam(), m_Memory);
		ASSERT_TRUE(bytes);
		bytes.Value()[8] = Byte{2};
		expectError(Shader::Load(bytes.Value().View(), m_Memory), EErrorCode::Unsupported);
		bytes.Value()[8] = Byte{1};
		Byte saved[4];
		CopyBytes(saved, bytes.Value().Data() + 12, 4);
		for (usize i = 12; i < 16; ++i)
		{
			bytes.Value()[i] = Byte{255};
		}
		expectError(Shader::Load(bytes.Value().View(), m_Memory), EErrorCode::InvalidArgument);
		CopyBytes(bytes.Value().Data() + 12, saved, 4);
		bytes.Value().AddRequired(Byte{0});
		expectError(Shader::Load(bytes.Value().View(), m_Memory), EErrorCode::InvalidArgument);
	}

	TEST_P(ShaderSerializationTest, ByteBudgetIsInclusive)
	{
		Result<Array<Byte>> bytes = readFixture(GetParam(), m_Memory);
		ASSERT_TRUE(bytes);
		ShaderLimits limits;
		limits.MaximumTotalBytes = bytes.Value().Size();
		EXPECT_TRUE(Shader::Load(bytes.Value().View(), m_Memory, limits));
		--limits.MaximumTotalBytes;
		expectError(Shader::Load(bytes.Value().View(), m_Memory, limits), EErrorCode::InvalidArgument);
	}

	INSTANTIATE_TEST_SUITE_P(Files, ShaderSerializationTest, testing::Values("Compute", "Surface", "Parameters"));

	TEST_F(ShaderTest, CancelledLoadSaveAndCreateLeaveInputsUsable)
	{
		Result<Array<Byte>> bytes = readFixture("Compute", m_Memory);
		ASSERT_TRUE(bytes);
		Result<Shader> asset = Shader::Load(bytes.Value().View(), m_Memory);
		ASSERT_TRUE(asset);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(asset.Value(), m_Memory);
		ASSERT_TRUE(draft);
		CancellationSource cancel(m_Memory);
		cancel.RequestCancellation();
		expectError(Shader::Load(bytes.Value().View(), m_Memory, {}, cancel.GetToken()), EErrorCode::Cancelled);
		expectError(asset.Value().Save(m_Memory, cancel.GetToken()), EErrorCode::Cancelled);
		expectError(Shader::Create(draft.Value(), m_Memory, {}, cancel.GetToken()), EErrorCode::Cancelled);
		EXPECT_TRUE(asset.Value().Save(m_Memory));
		EXPECT_TRUE(draft.Value().Validate());
	}

	TEST_F(ShaderTest, LoadAllocationSweepReleasesEveryPartialOwner)
	{
		Result<Array<Byte>> bytes = readFixture("Compute", m_Memory);
		ASSERT_TRUE(bytes);
		bool bSucceeded = false;
		const usize baseline = m_Memory.GetStatistics().LiveAllocationCount;
		for (usize count = 0; count < 2048; ++count)
		{
			{
				FailingMemoryResource failing(m_Memory, count);
				Result<Shader> result = Shader::Load(bytes.Value().View(), failing);
				if (result)
				{
					bSucceeded = true;
				}
				else
				{
					expectError(result, EErrorCode::ExternalFailure);
				}
			}
			ASSERT_EQ(m_Memory.GetStatistics().LiveAllocationCount, baseline) << count;
			if (bSucceeded)
			{
				break;
			}
		}
		EXPECT_TRUE(bSucceeded);
	}

	TEST_F(ShaderTest, SaveAllocationSweepPreservesAssetIdentity)
	{
		Result<Shader> asset = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(asset);
		const Hash256 identity = asset.Value().GetIdentity();
		bool bSucceeded = false;
		for (usize count = 0; count < 256; ++count)
		{
			FailingMemoryResource failing(m_Memory, count);
			Result<Array<Byte>> saved = asset.Value().Save(failing);
			if (saved)
			{
				bSucceeded = true;
				break;
			}
			expectError(saved, EErrorCode::ExternalFailure);
			EXPECT_EQ(asset.Value().GetIdentity(), identity);
		}
		EXPECT_TRUE(bSucceeded);
	}
	TEST_F(ShaderTest, CompiledOnlyCanonicalizationRemapsEntriesBindingsAndUses)
	{
		Result<Shader> fixture = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(fixture);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(fixture.Value(), m_Memory);
		ASSERT_TRUE(draft);
		draft.Value().Name.Clear();
		draft.Value().Kernels.Clear();
		draft.Value().Keywords.Clear();
		Result<Shader> original = Shader::Create(draft.Value(), m_Memory);
		ASSERT_TRUE(original);
		Swap(draft.Value().EntryPoints[0], draft.Value().EntryPoints[1]);
		for (ShaderVariant& variant : draft.Value().Variants)
		{
			variant.EntryPointIndex = 1 - variant.EntryPointIndex;
			Swap(variant.Bindings[0], variant.Bindings[2]);
			for (ShaderResourceUse& use : variant.ResourceUses)
			{
				if (use.BindingIndex == 0 || use.BindingIndex == 2)
				{
					use.BindingIndex = 2 - use.BindingIndex;
				}
			}
		}
		Swap(draft.Value().Variants[0], draft.Value().Variants[3]);
		Result<Shader> shuffled = Move(draft).TakeValue().Build(m_Memory);
		ASSERT_TRUE(shuffled);
		Result<Array<Byte>> first = original.Value().Save(m_Memory);
		Result<Array<Byte>> second = shuffled.Value().Save(m_Memory);
		ASSERT_TRUE(first);
		ASSERT_TRUE(second);
		expectBytes(first.Value().View(), second.Value().View());
		EXPECT_EQ(original.Value().GetIdentity(), shuffled.Value().GetIdentity());
		const Byte magic[] = {Byte{'R'}, Byte{'X'}, Byte{'S'}, Byte{'H'}, Byte{'A'}, Byte{'D'}, Byte{'E'}, Byte{'R'}};
		expectBytes({first.Value().Data(), 8}, magic);
		Result<Shader> loaded = Shader::Load(first.Value().View(), m_Memory);
		ASSERT_TRUE(loaded);
		EXPECT_EQ(loaded.Value().GetIdentity(), original.Value().GetIdentity());
		// The compiled stream starts with a fixed header and length-prefixed entry names.
		Array<Byte>& bytes = first.Value();
		uint32 nameLength = 0;
		CopyBytes(&nameLength, bytes.Data() + 60, 4);
		const usize stageOffset = 64 + nameLength;
		ASSERT_LT(stageOffset + 4, bytes.Size());
		Byte stage[4];
		CopyBytes(stage, bytes.Data() + stageOffset, 4);
		SetBytes(bytes.Data() + stageOffset, 255, 4);
		EXPECT_FALSE(Shader::Load(bytes.View(), m_Memory));
		CopyBytes(bytes.Data() + stageOffset, stage, 4);
		// Reverse the two same-length names without remapping references: noncanonical input.
		const usize secondName = stageOffset + 16 + 4;
		ASSERT_EQ(nameLength, 4u);
		const usize entryReference = secondName + nameLength + 16 + 32;
		ASSERT_LT(entryReference + 4, bytes.Size());
		Byte originalReference[4];
		CopyBytes(originalReference, bytes.Data() + entryReference, 4);
		SetBytes(bytes.Data() + entryReference, 255, 4);
		EXPECT_FALSE(Shader::Load(bytes.View(), m_Memory));
		CopyBytes(bytes.Data() + entryReference, originalReference, 4);
		Byte name[4];
		CopyBytes(name, bytes.Data() + 64, 4);
		CopyBytes(bytes.Data() + 64, bytes.Data() + secondName, 4);
		CopyBytes(bytes.Data() + secondName, name, 4);
		EXPECT_FALSE(Shader::Load(bytes.View(), m_Memory));
	}

}
