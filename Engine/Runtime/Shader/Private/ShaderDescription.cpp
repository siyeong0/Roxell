#include "ShaderDescription.h"

#include "ShaderSerialization.h"
#include "ShaderWire.hpp"

#include <ByteOperations.hpp>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		using Status = Result<void>;
		Status invalid(StringView message)
		{
			return Status::Failure(MakeArtifactError(message));
		}

		bool validText(StringView text, const ShaderLimits& limits, bool bAllowEmpty = false)
		{
			return (bAllowEmpty || !text.IsEmpty()) && text.Size() <= limits.MaximumNameBytes && text.Size() <= 4096 && !text.Contains('\0') && IsValidUtf8(text);
		}

		template <class T>
		bool uniqueNames(const Array<T>& values, const ShaderLimits& limits)
		{
			for (usize i = 0; i < values.Size(); ++i)
			{
				if (!validText(values[i].Name, limits))
				{
					return false;
				}
				for (usize j = 0; j < i; ++j)
				{
					if (values[i].Name == values[j].Name)
					{
						return false;
					}
				}
			}

			return true;
		}

		bool contains(Span<const String> values, StringView value)
		{
			for (const String& item : values)
			{
				if (item == value)
				{
					return true;
				}
			}

			return false;
		}

		bool sameKeywords(const ShaderCodeChoice& a, const ShaderCodeChoice& b)
		{
			if (a.Keywords.Size() != b.Keywords.Size())
			{
				return false;
			}
			for (const String& key : a.Keywords)
			{
				if (!contains(b.Keywords.View(), key))
				{
					return false;
				}
			}

			return true;
		}

		const ShaderEntryPoint* findEntry(const ShaderBuilder& builder, StringView name)
		{
			for (const ShaderEntryPoint& entry : builder.EntryPoints)
			{
				if (entry.Name == name)
				{
					return &entry;
				}
			}

			return nullptr;
		}

		Status validateChoices(const ShaderBuilder& builder, const Array<ShaderCodeChoice>& choices, bool bCompute, uint32& totalChoices, const ShaderLimits& limits)
		{
			if (choices.IsEmpty() || choices.Size() > 512 - totalChoices)
			{
				return invalid("Missing or excessive shader choices.");
			}
			totalChoices += static_cast<uint32>(choices.Size());
			for (usize i = 0; i < choices.Size(); ++i)
			{
				const ShaderCodeChoice& choice = choices[i];
				if (choice.Entries.IsEmpty() || choice.Entries.Size() > (bCompute ? 1u : 3u) || choice.Keywords.Size() > 64)
				{
					return invalid("Invalid pass/kernel entry or keyword count.");
				}
				for (usize j = 0; j < i; ++j)
				{
					if (sameKeywords(choice, choices[j]))
					{
						return invalid("Duplicate keyword combination.");
					}
				}
				for (usize k = 0; k < choice.Keywords.Size(); ++k)
				{
					if (!contains(builder.Keywords.View(), choice.Keywords[k]))
					{
						return invalid("Choice uses an undeclared keyword.");
					}
					for (usize j = 0; j < k; ++j)
					{
						if (choice.Keywords[k] == choice.Keywords[j])
						{
							return invalid("Duplicate choice keyword.");
						}
					}
				}

				uint64 stages = 0;
				for (const String& name : choice.Entries)
				{
					if (!validText(name, limits))
					{
						return invalid("Invalid choice entry name.");
					}

					const ShaderEntryPoint* entry = findEntry(builder, name);
					if (!entry)
					{
						return invalid("Choice references a missing compiled entry.");
					}

					const uint64 bit = uint64(1) << static_cast<uint32>(entry->Stage);
					if (stages & bit)
					{
						return invalid("Repeated stage in a pass.");
					}
					stages |= bit;
					if (bCompute ? entry->Stage != EShaderStage::Compute : (entry->Stage != EShaderStage::Vertex && entry->Stage != EShaderStage::Pixel && entry->Stage != EShaderStage::Mesh && entry->Stage != EShaderStage::Task))
					{
						return invalid("Entry stage does not belong to this pass/kernel.");
					}
					bool bHasCode = false;
					for (const ShaderVariant& variant : builder.Variants)
					{
						if (&builder.EntryPoints[variant.EntryPointIndex] == entry)
						{
							bHasCode = true;
						}
					}
					if (!bHasCode)
					{
						return invalid("Choice entry has no compiled code.");
					}
				}
				if (!bCompute)
				{
					const bool bVertex = (stages & (uint64(1) << static_cast<uint32>(EShaderStage::Vertex))) != 0;
					const bool bMesh = (stages & (uint64(1) << static_cast<uint32>(EShaderStage::Mesh))) != 0;
					const bool bTask = (stages & (uint64(1) << static_cast<uint32>(EShaderStage::Task))) != 0;
					if (bVertex == bMesh || (bTask && !bMesh))
					{
						return invalid("A pass requires either vertex or mesh, with task only before mesh.");
					}
				}
			}

			return Status::Success();
		}

		bool validState(const ShaderRenderState& state)
		{
			return state.Cull <= EShaderCullMode::Back && state.DepthCompare <= EShaderCompare::Always && state.SourceColor <= EShaderBlendFactor::OneMinusDestinationColor &&
			       state.DestinationColor <= EShaderBlendFactor::OneMinusDestinationColor && state.SourceAlpha <= EShaderBlendFactor::OneMinusDestinationColor && state.DestinationAlpha <= EShaderBlendFactor::OneMinusDestinationColor &&
			       state.ColorOperation <= EShaderBlendOperation::Maximum && state.AlphaOperation <= EShaderBlendOperation::Maximum && state.ColorWriteMask <= 15;
		}

		bool matchesProperty(const ShaderProperty& property, const ShaderVariant& variant, const ShaderPropertyBinding& mapping)
		{
			const ShaderBinding* binding = nullptr;
			for (const ShaderBinding& item : variant.Bindings)
			{
				if (item.Name == mapping.Binding)
				{
					binding = &item;
				}
			}
			if (!binding || binding->Scope != EShaderBindingScope::Global || binding->ArrayCount != 1 || binding->CountMode != EShaderDescriptorCountMode::Fixed)
			{
				return false;
			}

			const EShaderPropertyType type = property.GetType();
			if (type >= EShaderPropertyType::Texture2D)
			{
				if (!mapping.Field.IsEmpty())
				{
					return false;
				}
				if (type == EShaderPropertyType::Sampler)
				{
					return binding->Kind == EShaderBindingKind::Sampler;
				}
				if (type == EShaderPropertyType::Buffer)
				{
					return binding->Kind == EShaderBindingKind::ReadOnlyBuffer || binding->Kind == EShaderBindingKind::ReadWriteBuffer;
				}

				return binding->Kind == EShaderBindingKind::SampledTexture &&
					   !binding->bTextureArray && !binding->bMultisampled &&
				       binding->TextureShape == (type == EShaderPropertyType::Texture2D ? EShaderTextureShape::Texture2D : EShaderTextureShape::Cube);
			}
			if (binding->Kind != EShaderBindingKind::ConstantBuffer || binding->DataLayout >= variant.Layouts.Size())
			{
				return false;
			}

			const ShaderDataField* field = nullptr;
			for (const ShaderDataField& item : variant.Layouts[binding->DataLayout].Fields)
			{
				if (item.Name == mapping.Field)
				{
					field = &item;
				}
			}
			if (!field || field->NestedLayout != NO_DATA_LAYOUT || field->ArrayCount != 1 || field->ScalarWidth != 32)
			{
				return false;
			}

			EShaderScalar scalar = EShaderScalar::Float;
			if (type == EShaderPropertyType::Int)
			{
				scalar = EShaderScalar::SignedInteger;
			}
			else if (type == EShaderPropertyType::Bool)
			{
				scalar = EShaderScalar::Bool;
			}

			if (field->Scalar != scalar)
			{
				return false;
			}
			if (type == EShaderPropertyType::Matrix)
			{
				return field->MatrixRows == 4 && field->MatrixColumns == 4;
			}

			return field->MatrixRows == 1 && field->MatrixColumns == 1 && field->VectorWidth == ((type == EShaderPropertyType::Vector || type == EShaderPropertyType::Color) ? 4u : 1u);
		}

		Status malformed()
		{
			return Status::Failure(MakeArtifactError("Malformed or over-budget shader description."));
		}

		Status failedRead(const Reader& reader)
		{
			return reader.HasAllocationFailure() ? Status::Failure(AllocationError())
				: malformed();
		}

		void writeStrings(Writer& writer, const Array<String>& strings)
		{
			writer.U32(static_cast<uint32>(strings.Size()));
			for (const String& text : strings)
			{
				writer.Text(text);
			}
		}

		Status readStrings(Reader& reader, Array<String>& strings, uint32 maximum, const ShaderLimits& limits)
		{
			const uint32 count = reader.U32();
			uint64 total = 0;
			if (!reader.Count(count, total, maximum, 4))
			{
				return failedRead(reader);
			}
			for (uint32 i = 0; i < count; ++i)
			{
				String text(strings.GetResource());
				reader.Text(text, limits.MaximumNameBytes);
				if (!reader.IsValid())
				{
					return failedRead(reader);
				}
				if (!strings.Add(Move(text)))
				{
					return Status::Failure(AllocationError());
				}
			}

			return Status::Success();
		}

		void writeChoices(Writer& writer, const Array<ShaderCodeChoice>& choices)
		{
			writer.U32(static_cast<uint32>(choices.Size()));
			for (const ShaderCodeChoice& choice : choices)
			{
				writeStrings(writer, choice.Keywords);
				writeStrings(writer, choice.Entries);
			}
		}

		Status readChoices(Reader& reader, Array<ShaderCodeChoice>& choices, uint64& total, const ShaderLimits& limits)
		{
			const uint32 count = reader.U32();
			if (!reader.Count(count, total, 512, 8))
			{
				return failedRead(reader);
			}
			for (uint32 i = 0; i < count; ++i)
			{
				ShaderCodeChoice choice(choices.GetResource());
				Result<void> keys = readStrings(reader, choice.Keywords, 64, limits);
				if (!keys)
				{
					return keys;
				}

				Result<void> entries = readStrings(reader, choice.Entries, 3, limits);
				if (!entries)
				{
					return entries;
				}
				if (!choices.Add(Move(choice)))
				{
					return Status::Failure(AllocationError());
				}
			}

			return Status::Success();
		}

		void writeState(Writer& writer, const ShaderRenderState& value)
		{
			writer.U32(static_cast<uint32>(value.Cull));
			writer.U32(static_cast<uint32>(value.DepthCompare));
			writer.U32(static_cast<uint32>(value.bDepthWrite));
			writer.U32(static_cast<uint32>(value.bBlend));
			writer.U32(static_cast<uint32>(value.SourceColor));
			writer.U32(static_cast<uint32>(value.DestinationColor));
			writer.U32(static_cast<uint32>(value.ColorOperation));
			writer.U32(static_cast<uint32>(value.SourceAlpha));
			writer.U32(static_cast<uint32>(value.DestinationAlpha));
			writer.U32(static_cast<uint32>(value.AlphaOperation));
			writer.U32(static_cast<uint32>(value.ColorWriteMask));
		}

		Status readState(Reader& reader, ShaderRenderState& value)
		{
			value.Cull = reader.Enum<EShaderCullMode>();
			value.DepthCompare = reader.Enum<EShaderCompare>();
			const uint32 bDepthWrite = reader.U32();
			if (bDepthWrite > 1)
			{
				return malformed();
			}
			value.bDepthWrite = bDepthWrite != 0;
			const uint32 bBlend = reader.U32();
			if (bBlend > 1)
			{
				return malformed();
			}
			value.bBlend = bBlend != 0;
			value.SourceColor = reader.Enum<EShaderBlendFactor>();
			value.DestinationColor = reader.Enum<EShaderBlendFactor>();
			value.ColorOperation = reader.Enum<EShaderBlendOperation>();
			value.SourceAlpha = reader.Enum<EShaderBlendFactor>();
			value.DestinationAlpha = reader.Enum<EShaderBlendFactor>();
			value.AlphaOperation = reader.Enum<EShaderBlendOperation>();
			value.ColorWriteMask = reader.Enum<uint32>();
			if (!reader.IsValid())
			{
				return failedRead(reader);
			}

			return Status::Success();
		}
	} // namespace

	bool HasDescription(const ShaderBuilder& value) noexcept
	{
		return !value.Name.IsEmpty() || !value.Properties.IsEmpty() || !value.Passes.IsEmpty() || !value.Kernels.IsEmpty() || !value.Keywords.IsEmpty() || !value.PropertyBindings.IsEmpty();
	}

	Result<void> ValidateDescription(const ShaderBuilder& value, const ShaderLimits& limits)
	{
		if (!HasDescription(value))
		{
			return Status::Success();
		}
		if (!validText(value.Name, limits) || value.Properties.Size() > 256 || value.Passes.Size() > 64 || value.Kernels.Size() > 256 || value.Keywords.Size() > 64 ||
			value.PropertyBindings.Size() > 1024)
		{
			return invalid("Invalid shader name or authored table counts.");
		}
		if (!uniqueNames(value.Properties, limits) || !uniqueNames(value.Passes, limits) || !uniqueNames(value.Kernels, limits))
		{
			return invalid("Invalid or duplicate authored name.");
		}
		for (usize i = 0; i < value.Keywords.Size(); ++i)
		{
			if (!validText(value.Keywords[i], limits))
			{
				return invalid("Invalid keyword name.");
			}
			for (usize j = 0; j < i; ++j)
			{
				if (value.Keywords[i] == value.Keywords[j])
				{
					return invalid("Duplicate keyword name.");
				}
			}
		}
		for (const ShaderProperty& property : value.Properties)
		{
			if (!validText(property.DisplayName, limits, true) || property.GetType() > EShaderPropertyType::Sampler)
			{
				return invalid("Invalid property display name or type.");
			}

			const EShaderPropertyType type = property.GetType();
			uint32 lanes = 0;
			switch (type)
			{
			case EShaderPropertyType::Matrix:
				lanes = 16;
				break;

			case EShaderPropertyType::Vector:
			case EShaderPropertyType::Color:
				lanes = 4;
				break;

			default:
				if (type < EShaderPropertyType::Texture2D)
				{
					lanes = 1;
				}
				break;
			}

			for (uint32 i = 0; i < 16; ++i)
			{
				const uint32 bits = property.DefaultValue.Bits[i];
				if (i >= lanes && bits != 0)
				{
					return invalid("Unused property default lanes must be zero.");
				}
				if (type == EShaderPropertyType::Bool && bits > 1)
				{
					return invalid("Invalid boolean default.");
				}
				if (i < lanes && type != EShaderPropertyType::Int && type != EShaderPropertyType::Bool && (bits & 0x7f800000u) == 0x7f800000u)
				{
					return invalid("Property defaults must be finite.");
				}
			}

			uint32 minimumBits = 0;
			uint32 maximumBits = 0;
			CopyBytes(&minimumBits, &property.Minimum, 4);
			CopyBytes(&maximumBits, &property.Maximum, 4);
			if (property.bHasRange)
			{
				float32 defaultValue = 0;
				CopyBytes(&defaultValue, property.DefaultValue.Bits, 4);
				if (type != EShaderPropertyType::Float || (minimumBits & 0x7f800000u) == 0x7f800000u || (maximumBits & 0x7f800000u) == 0x7f800000u ||
					property.Minimum > property.Maximum ||
					defaultValue < property.Minimum ||
					defaultValue > property.Maximum)
				{
					return invalid("Invalid float property range/default.");
				}
			}
			else if (minimumBits || maximumBits)
			{
				return invalid("Absent ranges must have zero bounds.");
			}
		}

		uint32 choices = 0;
		for (const ShaderPass& pass : value.Passes)
		{
			if (!validText(pass.Tag, limits, true) || !validState(pass.RenderState))
			{
				return invalid("Invalid pass tag or render state.");
			}

			Result<void> checked = validateChoices(value, pass.Choices, false, choices, limits);
			if (!checked)
			{
				return checked;
			}
		}
		for (const ShaderKernel& kernel : value.Kernels)
		{
			Result<void> checked = validateChoices(value, kernel.Choices, true, choices, limits);
			if (!checked)
			{
				return checked;
			}
		}
		for (usize i = 0; i < value.PropertyBindings.Size(); ++i)
		{
			const ShaderPropertyBinding& mapping = value.PropertyBindings[i];
			const ShaderProperty* property = nullptr;
			for (const ShaderProperty& item : value.Properties)
			{
				if (item.Name == mapping.Property)
				{
					property = &item;
				}
			}

			const ShaderVariant* variant = nullptr;
			for (const ShaderVariant& item : value.Variants)
			{
				if (item.Target == mapping.Target && value.EntryPoints[item.EntryPointIndex].Name == mapping.Entry)
				{
					variant = &item;
				}
			}
			if (!property || !variant || !matchesProperty(*property, *variant, mapping))
			{
				return invalid("Property mapping does not match its exact compiled binding/field.");
			}
			for (usize j = 0; j < i; ++j)
			{
				const ShaderPropertyBinding& previous = value.PropertyBindings[j];
				if (mapping.Entry == previous.Entry && mapping.Target == previous.Target &&
					(mapping.Property == previous.Property || (mapping.Binding == previous.Binding && mapping.Field == previous.Field)))
				{
					return invalid("Duplicate or overlapping property mapping.");
				}
			}
		}
		// Every public property must have an explicit physical mapping. No reflection field is exposed
		// implicitly.
		for (const ShaderProperty& property : value.Properties)
		{
			bool bMapped = false;
			for (const ShaderPropertyBinding& mapping : value.PropertyBindings)
			{
				if (mapping.Property == property.Name)
				{
					bMapped = true;
				}
			}
			if (!bMapped)
			{
				return invalid("Public property has no compiled mapping.");
			}
		}

		return Status::Success();
	}

	Result<void> WriteDescription(Writer& writer, const ShaderBuilder& value)
	{
		writer.Text(value.Name);
		writeStrings(writer, value.Keywords);
		writer.U32(static_cast<uint32>(value.Properties.Size()));
		for (const ShaderProperty& property : value.Properties)
		{
			writer.Text(property.Name);
			writer.Text(property.DisplayName);
			writer.Enum(property.DefaultValue.Type);
			for (uint32 bits : property.DefaultValue.Bits)
			{
				writer.U32(bits);
			}
			writer.U32(property.bHasRange ? 1 : 0);
			uint32 bits = 0;
			CopyBytes(&bits, &property.Minimum, 4);
			writer.U32(bits);
			CopyBytes(&bits, &property.Maximum, 4);
			writer.U32(bits);
		}
		writer.U32(static_cast<uint32>(value.Passes.Size()));
		for (const ShaderPass& pass : value.Passes)
		{
			writer.Text(pass.Name);
			writer.Text(pass.Tag);
			writeState(writer, pass.RenderState);
			writeChoices(writer, pass.Choices);
		}
		writer.U32(static_cast<uint32>(value.Kernels.Size()));
		for (const ShaderKernel& kernel : value.Kernels)
		{
			writer.Text(kernel.Name);
			writeChoices(writer, kernel.Choices);
		}
		writer.U32(static_cast<uint32>(value.PropertyBindings.Size()));
		for (const ShaderPropertyBinding& mapping : value.PropertyBindings)
		{
			writer.Text(mapping.Property);
			writer.Text(mapping.Entry);
			WriteProfile(writer, mapping.Target);
			writer.Text(mapping.Binding);
			writer.Text(mapping.Field);
		}

		if (!writer.IsValid())
		{
			return Status::Failure(AllocationError());
		}

		return Status::Success();
	}

	Result<void> ReadDescription(Reader& reader, ShaderBuilder& value, const ShaderLimits& limits)
	{
		MemoryResource& memory = value.Properties.GetResource();
		reader.Text(value.Name, limits.MaximumNameBytes);
		Result<void> keys = readStrings(reader, value.Keywords, 64, limits);
		if (!keys)
		{
			return keys;
		}

		uint64 total = 0;
		const uint32 properties = reader.U32();
		if (!reader.Count(properties, total, 256, 88))
		{
			return failedRead(reader);
		}
		for (uint32 i = 0; i < properties; ++i)
		{
			ShaderProperty property(memory);
			reader.Text(property.Name, limits.MaximumNameBytes);
			reader.Text(property.DisplayName, limits.MaximumNameBytes);
			property.DefaultValue.Type = reader.Enum<EShaderPropertyType>();
			for (uint32& bits : property.DefaultValue.Bits)
			{
				bits = reader.U32();
			}

			const uint32 range = reader.U32();
			if (range > 1)
			{
				return malformed();
			}
			property.bHasRange = range != 0;
			uint32 bits = reader.U32();
			CopyBytes(&property.Minimum, &bits, 4);
			bits = reader.U32();
			CopyBytes(&property.Maximum, &bits, 4);
			if (!reader.IsValid())
			{
				return failedRead(reader);
			}
			if (!value.Properties.Add(Move(property)))
			{
				return Status::Failure(AllocationError());
			}
		}
		total = 0;
		uint64 choices = 0;
		const uint32 passes = reader.U32();
		if (!reader.Count(passes, total, 64, 56))
		{
			return failedRead(reader);
		}
		for (uint32 i = 0; i < passes; ++i)
		{
			ShaderPass pass(memory);
			reader.Text(pass.Name, limits.MaximumNameBytes);
			reader.Text(pass.Tag, limits.MaximumNameBytes);
			Result<void> state = readState(reader, pass.RenderState);
			if (!state)
			{
				return state;
			}

			Result<void> code = readChoices(reader, pass.Choices, choices, limits);
			if (!code)
			{
				return code;
			}
			if (!value.Passes.Add(Move(pass)))
			{
				return Status::Failure(AllocationError());
			}
		}
		total = 0;
		const uint32 kernels = reader.U32();
		if (!reader.Count(kernels, total, 256, 8))
		{
			return failedRead(reader);
		}
		for (uint32 i = 0; i < kernels; ++i)
		{
			ShaderKernel kernel(memory);
			reader.Text(kernel.Name, limits.MaximumNameBytes);
			Result<void> code = readChoices(reader, kernel.Choices, choices, limits);
			if (!code)
			{
				return code;
			}
			if (!value.Kernels.Add(Move(kernel)))
			{
				return Status::Failure(AllocationError());
			}
		}
		total = 0;
		const uint32 mappings = reader.U32();
		if (!reader.Count(mappings, total, 1024, 48))
		{
			return failedRead(reader);
		}
		for (uint32 i = 0; i < mappings; ++i)
		{
			ShaderPropertyBinding mapping(memory);
			reader.Text(mapping.Property, limits.MaximumNameBytes);
			reader.Text(mapping.Entry, limits.MaximumNameBytes);
			mapping.Target = ReadProfile(reader);
			reader.Text(mapping.Binding, limits.MaximumNameBytes);
			reader.Text(mapping.Field, limits.MaximumNameBytes);
			if (!reader.IsValid())
			{
				return failedRead(reader);
			}
			if (!value.PropertyBindings.Add(Move(mapping)))
			{
				return Status::Failure(AllocationError());
			}
		}

		if (!reader.IsValid())
		{
			return failedRead(reader);
		}

		return Status::Success();
	}

	Result<void> CloneDescription(const ShaderBuilder& source, ShaderBuilder& destination, const ShaderLimits& limits)
	{
		if (!HasDescription(source))
		{
			return Status::Success();
		}

		Writer writer(destination.Properties.GetResource());
		Result<void> written = WriteDescription(writer, source);
		if (!written)
		{
			return written;
		}

		Reader reader(writer.GetBytes().View());
		return ReadDescription(reader, destination, limits);
	}

	Result<Array<Byte>> WrapShaderDescription(Array<Byte> raw, const ShaderBuilder& value, MemoryResource& memory)
	{
		using R = Result<Array<Byte>>;
		if (!HasDescription(value))
		{
			return R::Success(Move(raw));
		}

		Writer writer(memory);
		writer.Raw("RXSHSET1", 8);
		writer.U32(1);
		writer.U32(static_cast<uint32>(raw.Size()));
		writer.Raw(raw.Data(), raw.Size());
		Result<void> written = WriteDescription(writer, value);
		if (!written)
		{
			return R::Failure(written.GetError());
		}
		if (writer.GetBytes().Size() > ShaderLimits{}.MaximumTotalBytes)
		{
			return R::Failure(MakeArtifactError("Shader asset exceeds the byte budget."));
		}

		return R::Success(Move(writer.GetBytes()));
	}

	Result<ShaderBuilder> LoadShaderDescription(Span<const Byte> bytes, MemoryResource& memory, const ShaderLimits& limits, CancellationToken cancellation)
	{
		using R = Result<ShaderBuilder>;
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(CancelledError());
		}
		if (bytes.Size() < 8 || CompareBytes(bytes.Data(), "RXSHSET1", 8) != 0)
		{
			return DecodeShaderArtifactDraft(bytes, memory, limits, cancellation);
		}
		if (bytes.Size() > limits.MaximumTotalBytes || bytes.Size() > ShaderLimits{}.MaximumTotalBytes)
		{
			return R::Failure(MakeArtifactError("Shader asset exceeds the byte budget."));
		}

		Reader reader(bytes);
		(void)reader.Raw(8);
		if (reader.U32() != 1)
		{
			return R::Failure(MakeArtifactError("Unsupported shader asset version.", EErrorCode::Unsupported));
		}

		const uint32 rawSize = reader.U32();
		const Span<const Byte> raw = reader.Raw(rawSize);
		if (!reader.IsValid())
		{
			return R::Failure(MakeArtifactError("Truncated shader asset."));
		}

		Result<ShaderBuilder> decoded = DecodeShaderArtifactDraft(raw, memory, limits, cancellation);
		if (!decoded)
		{
			return decoded;
		}

		Result<void> description = ReadDescription(reader, decoded.Value(), limits);
		if (!description)
		{
			return R::Failure(description.GetError());
		}
		if (!reader.IsValid() || reader.GetCursor().GetRemaining() != 0 || !HasDescription(decoded.Value()))
		{
			return R::Failure(MakeArtifactError("Malformed or trailing shader description."));
		}

		Result<void> valid = decoded.Value().Validate(limits);
		if (!valid)
		{
			return R::Failure(valid.GetError());
		}
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(CancelledError());
		}

		return decoded;
	}
} // namespace rox::shader
