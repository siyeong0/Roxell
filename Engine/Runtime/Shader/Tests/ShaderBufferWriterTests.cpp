#include "ShaderTestData.h"

#include <ShaderBufferWriter.h>

namespace rox::shader::test
{
	using namespace core;

	namespace
	{
		ShaderDataField field(StringView name, uint64 offset, uint64 size, uint32 width, MemoryResource& memory)
		{
			ShaderDataField value(memory);
			value.Name.AssignRequired(name);
			value.ByteOffset = offset;
			value.ByteSize = size;
			value.Scalar = EShaderScalar::Float;
			value.ScalarWidth = 32;
			value.VectorWidth = width;
			return value;
		}

		Array<ShaderDataLayout> hostLayouts(MemoryResource& memory)
		{
			Array<ShaderDataLayout> layouts(memory);
			ShaderDataLayout nested(memory);
			nested.Alignment = 4;
			nested.ByteSize = 16;
			nested.Fields.AddRequired(field("Color", 0, 16, 4, memory));
			layouts.AddRequired(Move(nested));
			ShaderDataLayout root(memory);
			root.Alignment = 4;
			root.ByteSize = 60;
			root.Fields.AddRequired(field("Tint", 0, 16, 4, memory));
			root.Fields.AddRequired(field("Exposure", 16, 4, 1, memory));
			ShaderDataField weights = field("Weights", 20, 8, 1, memory);
			weights.ArrayCount = 2;
			weights.ArrayStride = 4;
			root.Fields.AddRequired(Move(weights));
			ShaderDataField matrix = field("Transform", 28, 16, 1, memory);
			matrix.MatrixRows = 2;
			matrix.MatrixColumns = 2;
			matrix.MatrixOrder = EShaderMatrixOrder::ColumnMajor;
			matrix.MatrixStride = 8;
			root.Fields.AddRequired(Move(matrix));
			ShaderDataField child = field("Details", 44, 16, 1, memory);
			child.Scalar = EShaderScalar::None;
			child.ScalarWidth = 0;
			child.NestedLayout = 0;
			root.Fields.AddRequired(Move(child));
			layouts.AddRequired(Move(root));
			return layouts;
		}

		uint32 constantLayout(const ShaderVariant& variant)
		{
			for (const ShaderBinding& binding : variant.Bindings)
			{
				if (binding.Kind == EShaderBindingKind::ConstantBuffer)
				{
					return binding.DataLayout;
				}
			}
			return NO_DATA_LAYOUT;
		}

		void sentinel(Span<Byte> bytes)
		{
			for (Byte& byte : bytes)
			{
				byte = Byte{0xa5};
			}
		}

		void expectSentinel(Span<const Byte> bytes)
		{
			for (Byte byte : bytes)
			{
				EXPECT_EQ(byte, Byte{0xa5});
			}
		}
	}

	class ShaderPackingTest : public ShaderTest, public testing::WithParamInterface<EShaderTarget>
	{
	};

	TEST_P(ShaderPackingTest, RealReflectionMatchesDocumentedConstantBufferOffsets)
	{
		Result<Shader> asset = loadFixture("Parameters", m_Memory);
		ASSERT_TRUE(asset);
		Result<uint32> index = asset.Value().FindVariant({"Main", profile(GetParam())});
		ASSERT_TRUE(index);
		const ShaderVariant& variant = asset.Value().GetVariants()[index.Value()];
		const uint32 layoutIndex = constantLayout(variant);
		ASSERT_NE(layoutIndex, NO_DATA_LAYOUT);
		const ShaderDataLayout& layout = variant.Layouts[layoutIndex];
		EXPECT_EQ(layout.ByteSize, 112u);
		const char* names[] = {"Tint", "Exposure", "Weights", "Transform", "Details"};
		const uint64 offsets[] = {0, 16, 32, 64, 96};
		ASSERT_EQ(layout.Fields.Size(), 5u);
		for (usize i = 0; i < 5; ++i)
		{
			EXPECT_EQ(layout.Fields[i].Name, names[i]);
			EXPECT_EQ(layout.Fields[i].ByteOffset, offsets[i]);
		}
		EXPECT_EQ(layout.Fields[2].ArrayStride, 16u);
		EXPECT_EQ(layout.Fields[3].MatrixStride, 16u);
		EXPECT_EQ(layout.Fields[3].MatrixOrder, EShaderMatrixOrder::RowMajor);
	}

	TEST_P(ShaderPackingTest, PacksNestedArraysAndColumnMajorHostIntoExactGoldenBytes)
	{
		Result<Shader> asset = loadFixture("Parameters", m_Memory);
		ASSERT_TRUE(asset);
		Result<uint32> index = asset.Value().FindVariant({"Main", profile(GetParam())});
		ASSERT_TRUE(index);
		const uint32 layout = constantLayout(asset.Value().GetVariants()[index.Value()]);
		FailingMemoryResource failing(m_Memory, 1024);
		ShaderBufferWriter writer;
		{
			Array<ShaderDataLayout> host = hostLayouts(m_Memory);
			Result<ShaderBufferWriter> created = ShaderBufferWriter::Create(asset.Value(), index.Value(), layout, {host.View(), 1}, failing);
			ASSERT_TRUE(created) << created.GetError().Message.CStr();
			writer = Move(created).TakeValue();
		}
		EXPECT_EQ(writer.GetSourceByteSize(), 60u);
		EXPECT_EQ(writer.GetDestinationByteSize(), 112u);
		asset.Value() = {};
		failing.Reset(0);
		const float32 source[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
		Byte output[128];
		sentinel(output);
		Byte expected[112] = {};
		// Independent byte oracle: HLSL constant-buffer lanes, including transposed matrix lanes.
		const uint32 offsets[] = {0, 4, 8, 12, 16, 32, 48, 64, 68, 80, 84, 96, 100, 104, 108};
		const float32 values[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 9, 11, 12, 13, 14, 15};
		for (usize i = 0; i < 15; ++i)
		{
			CopyBytes(expected + offsets[i], &values[i], 4);
		}
		ASSERT_TRUE(writer.WriteStruct({reinterpret_cast<const Byte*>(source), sizeof(source)}, {output + 8, 120}));
		expectBytes({output + 8, 112}, expected);
		expectSentinel({output, 8});
		expectSentinel({output + 120, 8});
		EXPECT_EQ(failing.GetAllocationAttemptCount(), 0u);
		// The resource must outlive the plan even when recording allocations are forbidden.
		writer = {};
	}

	TEST_P(ShaderPackingTest, ShortAndOverlappingSpansFailWithoutWriting)
	{
		Result<Shader> asset = loadFixture("Parameters", m_Memory);
		ASSERT_TRUE(asset);
		Result<uint32> index = asset.Value().FindVariant({"Main", profile(GetParam())});
		ASSERT_TRUE(index);
		Array<ShaderDataLayout> host = hostLayouts(m_Memory);
		Result<ShaderBufferWriter> writer = ShaderBufferWriter::Create(
			asset.Value(),
			index.Value(),
			constantLayout(asset.Value().GetVariants()[index.Value()]),
			{host.View(), 1},
			m_Memory);
		ASSERT_TRUE(writer);
		Byte source[60] = {};
		Byte output[128];
		sentinel(output);
		expectError(writer.Value().WriteStruct({source, 59}, output), EErrorCode::InvalidArgument);
		expectError(writer.Value().WriteStruct(source, {output, 111}), EErrorCode::InvalidArgument);
		expectError(writer.Value().WriteStruct({output, 60}, {output + 4, 112}), EErrorCode::InvalidArgument);
		expectSentinel(output);
	}

	TEST_P(ShaderPackingTest, WriteValuesUsesActualNestedNumericLayoutAndPreservesGuards)
	{
		Result<Shader> asset = loadFixture("Parameters", m_Memory);
		ASSERT_TRUE(asset);
		Result<uint32> index = asset.Value().FindVariant({"Main", profile(GetParam())});
		ASSERT_TRUE(index);
		const ShaderVariant& variant = asset.Value().GetVariants()[index.Value()];
		uint32 nested = NO_DATA_LAYOUT;
		for (uint32 i = 0; i < variant.Layouts.Size(); ++i)
		{
			if (variant.Layouts[i].Fields.Size() == 1 && variant.Layouts[i].Fields[0].Name == "Color")
			{
				nested = i;
			}
		}
		ASSERT_NE(nested, NO_DATA_LAYOUT);
		const ShaderBufferValue value{"Color", ShaderPropertyValue::Color(1, 2, 3, 4)};
		Byte output[24];
		sentinel(output);
		ASSERT_TRUE(ShaderBufferWriter::WriteValues(asset.Value(), index.Value(), nested, {&value, 1}, {output + 4, 20}, m_Memory));
		const float32 expected[] = {1, 2, 3, 4};
		expectBytes({output + 4, 16}, {reinterpret_cast<const Byte*>(expected), 16});
		expectSentinel({output, 4});
		expectSentinel({output + 20, 4});
		sentinel(output);
		expectError(ShaderBufferWriter::WriteValues(asset.Value(), index.Value(), nested, {}, output, m_Memory), EErrorCode::InvalidArgument);
		expectSentinel(output);
		FailingMemoryResource failing(m_Memory, 0);
		expectError(ShaderBufferWriter::WriteValues(asset.Value(), index.Value(), nested, {&value, 1}, output, failing), EErrorCode::ExternalFailure);
		expectSentinel(output);
	}

	INSTANTIATE_TEST_SUITE_P(Targets, ShaderPackingTest, testing::Values(EShaderTarget::Dxil, EShaderTarget::SpirV));

	class InvalidHostLayoutTest : public ShaderTest, public testing::WithParamInterface<int>
	{
	};

	TEST_P(InvalidHostLayoutTest, RejectsInvalidHostShapeAndPackingBudgets)
	{
		Result<Shader> asset = loadFixture("Parameters", m_Memory);
		ASSERT_TRUE(asset);
		Result<uint32> index = asset.Value().FindVariant({"Main", {}});
		ASSERT_TRUE(index);
		Array<ShaderDataLayout> host = hostLayouts(m_Memory);
		ShaderPackingLimits limits;
		switch (GetParam())
		{
		case 0:
			host[1].Fields[1].ByteOffset = 0;
			break;
		case 1:
			host[1].Fields[1].Name.AssignRequired("Tint");
			break;
		case 2:
			host[1].Fields[0].Scalar = EShaderScalar::UnsignedInteger;
			break;
		case 3:
			host[1].Fields[4].NestedLayout = 1;
			break;
		case 4:
			host[1].Alignment = 3;
			break;
		case 5:
			host[1].Fields.RemoveAt(0);
			break;
		case 6:
			limits.MaximumOperations = 1;
			break;
		case 7:
			limits.MaximumBytes = 16;
			break;
		case 8:
			host[1].Fields[2].ArrayStride = 0;
			break;
		case 9:
			host[1].Fields[4].NestedLayout = 999;
			break;
		default:
			FAIL();
		}
		Result<ShaderBufferWriter> writer = ShaderBufferWriter::Create(
			asset.Value(),
			index.Value(),
			constantLayout(asset.Value().GetVariants()[index.Value()]),
			{host.View(), 1},
			m_Memory,
			limits);
		expectError(writer, EErrorCode::InvalidArgument);
	}

	INSTANTIATE_TEST_SUITE_P(InvalidRecords, InvalidHostLayoutTest, testing::Range(0, 10));
	TEST_P(ShaderPackingTest, PackingCreationAllocationSweepReleasesPartialPlans)
	{
		Result<Shader> asset = loadFixture("Parameters", m_Memory);
		ASSERT_TRUE(asset);
		Result<uint32> index = asset.Value().FindVariant({"Main", profile(GetParam())});
		ASSERT_TRUE(index);
		Array<ShaderDataLayout> host = hostLayouts(m_Memory);
		const uint32 layout = constantLayout(asset.Value().GetVariants()[index.Value()]);
		const usize baseline = m_Memory.GetStatistics().LiveAllocationCount;
		bool bSucceeded = false;
		for (usize count = 0; count < 1024; ++count)
		{
			{
				FailingMemoryResource failing(m_Memory, count);
				Result<ShaderBufferWriter> writer = ShaderBufferWriter::Create(asset.Value(), index.Value(), layout, {host.View(), 1}, failing);
				if (writer)
				{
					bSucceeded = true;
					EXPECT_EQ(writer.Value().GetDestinationByteSize(), 112u);
				}
				else
				{
					expectError(writer, EErrorCode::ExternalFailure);
				}
			}
			ASSERT_EQ(m_Memory.GetStatistics().LiveAllocationCount, baseline);
			if (bSucceeded)
			{
				break;
			}
		}
		EXPECT_TRUE(bSucceeded);
	}

}
