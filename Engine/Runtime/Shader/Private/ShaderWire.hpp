#pragma once

#include "Reader.h"
#include "Writer.h"

#include <Algorithms.hpp>
#include <ByteReader.h>
#include <ByteWriter.h>
#include <Numeric.hpp>
#include <ShaderBuilder.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utf8.h>

namespace rox::shader
{
	inline core::uint64 GetBindingRegisterExtent(const ShaderBinding& binding) noexcept
	{
		if (binding.RegisterNamespace == EShaderRegisterClass::Unified)
		{
			return 1;
		}

		if (binding.CountMode == EShaderDescriptorCountMode::Runtime)
		{
			return core::uint64{0x100000000} - binding.Binding;
		}

		return binding.ArrayCount;
	}

	inline core::Error MakeArtifactError(core::StringView message, core::EErrorCode code = core::EErrorCode::InvalidArgument)
	{
		return core::MakeError(code, "ROX-SHADER-ARTIFACT", message);
	}

	inline core::Error AllocationError()
	{
		return MakeArtifactError("Shader storage allocation failed.", core::EErrorCode::ExternalFailure);
	}

	inline core::Error CancelledError()
	{
		return MakeArtifactError("Shader operation was cancelled.", core::EErrorCode::Cancelled);
	}

	inline void WriteProfile(Writer& writer, const ShaderTargetProfile& profile)
	{
		writer.Enum(profile.Target);
		writer.U32(profile.ShaderModelMajor);
		writer.U32(profile.ShaderModelMinor);
		writer.U32(profile.SpirVMajor);
		writer.U32(profile.SpirVMinor);
		writer.U32(profile.EnvironmentMajor);
		writer.U32(profile.EnvironmentMinor);
		writer.U32(profile.ProfileVersion);
	}

	inline ShaderTargetProfile ReadProfile(Reader& reader)
	{
		ShaderTargetProfile profile;
		profile.Target = reader.Enum<EShaderTarget>();
		profile.ShaderModelMajor = reader.U32();
		profile.ShaderModelMinor = reader.U32();
		profile.SpirVMajor = reader.U32();
		profile.SpirVMinor = reader.U32();
		profile.EnvironmentMajor = reader.U32();
		profile.EnvironmentMinor = reader.U32();
		profile.ProfileVersion = reader.U32();
		return profile;
	}

	inline void WriteField(Writer& writer, const ShaderDataField& field, core::uint32 nested)
	{
		writer.Text(field.Name);
		writer.U64(field.ByteOffset);
		writer.U64(field.ByteSize);
		writer.Enum(field.Scalar);
		writer.U32(field.ScalarWidth);
		writer.U32(field.VectorWidth);
		writer.U32(field.MatrixRows);
		writer.U32(field.MatrixColumns);
		writer.Enum(field.MatrixOrder);
		writer.U64(field.MatrixStride);
		writer.U32(field.ArrayCount);
		writer.U64(field.ArrayStride);
		writer.U32(nested);
	}

	template <class T, class Less>
	bool OrderIndices(const core::Array<T>& values, core::Array<core::uint32>& order, Less less)
	{
		if (!order.Resize(values.Size()))
		{
			return false;
		}

		for (core::usize i = 0; i < values.Size(); ++i)
		{
			order[i] = static_cast<core::uint32>(i);
		}

		if (order.Size() > 1)
		{
			core::Sort(order.begin(), order.end(), [&](core::uint32 left, core::uint32 right) { return less(values[left], values[right]); });
		}

		return true;
	}

	inline bool FieldLess(const ShaderDataField& left, const ShaderDataField& right)
	{
		return left.ByteOffset != right.ByteOffset ? left.ByteOffset < right.ByteOffset : left.Name.View() < right.Name.View();
	}

	inline bool WriteLayoutBody(Writer& writer, const ShaderDataLayout& layout, const core::Array<core::uint32>* layoutMap = nullptr)
	{
		writer.U64(layout.ByteSize);
		writer.U32(layout.Alignment);
		writer.U64(layout.ElementStride);
		writer.U32(static_cast<core::uint32>(layout.Fields.Size()));

		core::Array<core::uint32> fields(writer.GetBytes().GetResource());
		if (!OrderIndices(layout.Fields, fields, FieldLess))
		{
			writer.MarkInvalid();
			return false;
		}

		for (core::uint32 i : fields)
		{
			const ShaderDataField& field = layout.Fields[i];
			WriteField(writer, field, field.NestedLayout == NO_DATA_LAYOUT || layoutMap == nullptr ? field.NestedLayout : (*layoutMap)[field.NestedLayout]);
		}

		return writer.IsValid();
	}
} // namespace rox::shader
