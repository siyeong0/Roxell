#pragma once
#include <RhiTypes.h>
#include <Types.h>
namespace rox::rhi
{
	enum class ECoopVecDataType : core::uint8
	{
		UInt8,
		SInt8,
		UInt8Packed,
		SInt8Packed,
		UInt16,
		SInt16,
		UInt32,
		SInt32,
		UInt64,
		SInt64,
		FloatE4M3,
		FloatE5M2,
		Float16,
		BFloat16,
		Float32,
		Float64
	};

	enum class ECoopVecMatrixLayout : core::uint8
	{
		RowMajor,
		ColumnMajor,
		InferencingOptimal,
		TrainingOptimal
	};

	struct CoopVecFormatCombo final
	{
		ECoopVecDataType InputType = ECoopVecDataType::UInt8;
		ECoopVecDataType InputInterpretation = ECoopVecDataType::UInt8;
		ECoopVecDataType MatrixInterpretation = ECoopVecDataType::UInt8;
		ECoopVecDataType BiasInterpretation = ECoopVecDataType::UInt8;
		ECoopVecDataType OutputType = ECoopVecDataType::UInt8;

		bool bTransposeSupported = false;
	};

	enum class ECoopVecShaderDialect : core::uint8
	{
		None,
		SpirvNV,
		DirectXLinAlg
	};

	struct CoopVecFeatures final
	{
		core::uint32 FormatCount = 0;

		bool bTrainingFloat16 = false;
		bool bTrainingFloat32 = false;

		core::uint32 MatrixStrideAlignmentBytes = 1;
		core::uint32 MatrixSizeAlignmentBytes = 1;
		core::uint32 MatrixOffsetAlignmentBytes = 64;

		ECoopVecShaderDialect ShaderDialect = ECoopVecShaderDialect::None;
	};

	struct CoopVecMatrixLocation final
	{
		BufferHandle Buffer;
		core::uint64 OffsetBytes = 0;
		core::uint64 ByteCount = 0;

		ECoopVecDataType Type = ECoopVecDataType::UInt8;
		ECoopVecMatrixLayout Layout = ECoopVecMatrixLayout::RowMajor;
		core::uint64 StrideBytes = 0;
	};

	struct CoopVecMatrixConversion final
	{
		CoopVecMatrixLocation Source;
		CoopVecMatrixLocation Destination;

		core::uint32 Rows = 0;
		core::uint32 Columns = 0;
	};
}
