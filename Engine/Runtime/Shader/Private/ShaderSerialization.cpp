#include "ShaderSerialization.h"

#include "CanonicalArtifactOrder.h"
#include "ShaderDescription.h"
#include "ShaderMetadata.h"
#include "ShaderRayMetadata.h"
#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <ByteOperations.hpp>
#include <Cancellation.h>
#include <ShaderBuilder.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		constexpr uint32 SHADER_FORMAT_VERSION = 1;

		void readField(Reader& reader, ShaderDataField& field, const ShaderLimits& limits)
		{
			reader.Text(field.Name, limits.MaximumNameBytes);
			field.ByteOffset = reader.U64();
			field.ByteSize = reader.U64();
			field.Scalar = reader.Enum<EShaderScalar>();
			field.ScalarWidth = reader.U32();
			field.VectorWidth = reader.U32();
			field.MatrixRows = reader.U32();
			field.MatrixColumns = reader.U32();
			field.MatrixOrder = reader.Enum<EShaderMatrixOrder>();
			field.MatrixStride = reader.U64();
			field.ArrayCount = reader.U32();
			field.ArrayStride = reader.U64();
			field.NestedLayout = reader.U32();
		}
	} // namespace

	Result<void> WriteCanonicalArtifact(Writer& writer, const ShaderBuilder& draft, CancellationToken cancellation)
	{
		const uint32 abiVersion = draft.AbiVersion;
		const EShaderFormat format = static_cast<EShaderFormat>(abiVersion);
		const Hash256& provenance = draft.BuildProvenance;
		const Span<const ShaderEntryPoint> entries = draft.EntryPoints.View();
		const Span<const ShaderVariant> variants = draft.Variants.View();

		if (cancellation.IsCancellationRequested())
		{
			return Result<void>::Failure(CancelledError());
		}

		if (format != EShaderFormat::V1 || abiVersion != SHADER_ABI_VERSION)
		{
			return Result<void>::Failure(MakeArtifactError("Only the current shader v1 format is supported."));
		}

		writer.Raw("RXSHADER", 8);
		writer.U32(static_cast<uint32>(format));
		writer.U32(static_cast<uint32>(format));
		writer.U32(0);
		writer.Hash(provenance);
		writer.U32(static_cast<uint32>(entries.Size()));
		writer.U32(static_cast<uint32>(variants.Size()));
		for (const ShaderEntryPoint& entry : entries)
		{
			writer.Text(entry.Name);
			writer.Enum(entry.Stage);
			for (uint32 size : entry.ThreadGroupSize)
			{
				writer.U32(size);
			}
		}

		for (const ShaderVariant& variant : variants)
		{
			if (cancellation.IsCancellationRequested())
			{
				return Result<void>::Failure(CancelledError());
			}

			WriteProfile(writer, variant.Target);
			writer.U32(variant.EntryPointIndex);
			writer.Text(variant.EmittedEntryPoint);
			writer.U64(variant.RequiredCapabilities);
			writer.U32(static_cast<uint32>(variant.Bytecode.Size()));
			writer.Raw(variant.Bytecode.Data(), variant.Bytecode.Size());
			writer.U32(static_cast<uint32>(variant.Layouts.Size()));
			writer.U32(static_cast<uint32>(variant.Bindings.Size()));
			writer.U32(static_cast<uint32>(variant.ResourceUses.Size()));
			for (const ShaderDataLayout& layout : variant.Layouts)
			{
				writer.Hash(layout.Identity);
				writer.U64(layout.ByteSize);
				writer.U32(layout.Alignment);
				writer.U64(layout.ElementStride);
				writer.U32(static_cast<uint32>(layout.Fields.Size()));
				for (const ShaderDataField& field : layout.Fields)
				{
					WriteField(writer, field, field.NestedLayout);
				}
			}

			for (const ShaderBinding& binding : variant.Bindings)
			{
				writer.Text(binding.Name);
				writer.Enum(binding.Kind);
				writer.U32(binding.Space);
				writer.U32(binding.Binding);
				writer.Enum(binding.RegisterNamespace);
				writer.U32(binding.ArrayCount);
				writer.U32(binding.DataLayout);
				writer.Enum(binding.TextureShape);
				writer.U32(binding.bTextureArray ? 1 : 0);
				writer.U32(binding.bMultisampled ? 1 : 0);
				writer.Enum(binding.Access);
				{
					writer.Enum(binding.CountMode);
				}
				{
					writer.Enum(binding.Scope);
				}
			}

			for (const ShaderResourceUse& use : variant.ResourceUses)
			{
				writer.U32(use.BindingIndex);
				writer.Enum(use.Access);
			}
			{
				Result<void> metadata = WriteVariantMetadata(writer, variant, writer.GetBytes().GetResource(), nullptr, static_cast<uint32>(format));
				if (!metadata)
				{
					return metadata;
				}
			}
		}

		if (cancellation.IsCancellationRequested())
		{
			return Result<void>::Failure(CancelledError());
		}

		if (!writer.IsValid())
		{
			return Result<void>::Failure(AllocationError());
		}

		return Result<void>::Success();
	}

	Result<Array<Byte>> SaveShader(const ShaderBuilder& artifact, MemoryResource& resource, CancellationToken cancellation)
	{
		return SaveShader(artifact, static_cast<EShaderFormat>(artifact.AbiVersion), resource, cancellation);
	}

	Result<Array<Byte>> SaveShader(const ShaderBuilder& artifact, EShaderFormat format, MemoryResource& resource, CancellationToken cancellation)
	{
		using Output = Result<Array<Byte>>;
		if (cancellation.IsCancellationRequested())
		{
			return Output::Failure(CancelledError());
		}

		Result<void> valid = artifact.Validate();
		if (!valid)
		{
			return Output::Failure(valid.GetError());
		}

		if (format != EShaderFormat::V1)
		{
			return Output::Failure(MakeArtifactError("Only the current shader v1 format is supported."));
		}

		CanonicalArtifactOrder order(resource);
		Result<void> initialized = order.Initialize(artifact, cancellation);
		if (!initialized)
		{
			return Output::Failure(initialized.GetError());
		}

		const Array<uint32>& entries = order.GetEntries();
		const Array<uint32>& entryMap = order.GetEntryMap();
		const Array<uint32>& variants = order.GetVariants();
		Writer writer(resource);
		writer.Raw("RXSHADER", 8);
		writer.U32(static_cast<uint32>(format));
		writer.U32(static_cast<uint32>(format));
		writer.U32(0);
		writer.Hash(artifact.BuildProvenance);
		writer.U32(static_cast<uint32>(entries.Size()));
		writer.U32(static_cast<uint32>(variants.Size()));
		for (uint32 entryIndex : entries)
		{
			const ShaderEntryPoint& entry = artifact.EntryPoints[entryIndex];
			writer.Text(entry.Name);
			writer.Enum(entry.Stage);
			for (uint32 size : entry.ThreadGroupSize)
			{
				writer.U32(size);
			}
		}

		for (uint32 variantIndex : variants)
		{
			if (cancellation.IsCancellationRequested())
			{
				return Output::Failure(CancelledError());
			}

			const ShaderVariant& variant = artifact.Variants[variantIndex];
			const CanonicalVariantOrder& variantOrder = order.GetVariantOrder(variantIndex);
			const Array<uint32>& layouts = variantOrder.Layouts;
			const Array<uint32>& layoutMap = variantOrder.LayoutMap;
			const Array<uint32>& bindings = variantOrder.Bindings;
			const Array<uint32>& bindingMap = variantOrder.BindingMap;
			const Array<uint32>& uses = variantOrder.Uses;
			WriteProfile(writer, variant.Target);
			writer.U32(entryMap[variant.EntryPointIndex]);
			writer.Text(variant.EmittedEntryPoint);
			writer.U64(variant.RequiredCapabilities);
			writer.U32(static_cast<uint32>(variant.Bytecode.Size()));
			writer.Raw(variant.Bytecode.Data(), variant.Bytecode.Size());
			writer.U32(static_cast<uint32>(layouts.Size()));
			writer.U32(static_cast<uint32>(bindings.Size()));
			writer.U32(static_cast<uint32>(uses.Size()));
			for (uint32 layoutIndex : layouts)
			{
				Writer body(resource);
				WriteLayoutBody(body, variant.Layouts[layoutIndex], &layoutMap);
				if (!body.IsValid())
				{
					return Output::Failure(AllocationError());
				}

				Writer identity(resource);
				identity.Raw("ROX-SHADER-LAYOUT", 17);
				identity.U32(SHADER_ABI_VERSION);
				identity.Raw(body.GetBytes().Data(), body.GetBytes().Size());
				if (!identity.IsValid())
				{
					return Output::Failure(AllocationError());
				}

				writer.Hash(ComputeSha256({identity.GetBytes().Data(), identity.GetBytes().Size()}));
				writer.Raw(body.GetBytes().Data(), body.GetBytes().Size());
			}

			for (uint32 bindingIndex : bindings)
			{
				const ShaderBinding& binding = variant.Bindings[bindingIndex];
				writer.Text(binding.Name);
				writer.Enum(binding.Kind);
				writer.U32(binding.Space);
				writer.U32(binding.Binding);
				writer.Enum(binding.RegisterNamespace);
				writer.U32(binding.ArrayCount);
				writer.U32(binding.DataLayout == NO_DATA_LAYOUT ? NO_DATA_LAYOUT : layoutMap[binding.DataLayout]);
				writer.Enum(binding.TextureShape);
				writer.U32(binding.bTextureArray ? 1 : 0);
				writer.U32(binding.bMultisampled ? 1 : 0);
				writer.Enum(binding.Access);
				{
					writer.Enum(binding.CountMode);
				}
				{
					writer.Enum(binding.Scope);
				}
			}

			for (uint32 useIndex : uses)
			{
				writer.U32(bindingMap[variant.ResourceUses[useIndex].BindingIndex]);
				writer.Enum(variant.ResourceUses[useIndex].Access);
			}
			{
				Result<void> metadata = WriteVariantMetadata(writer, variant, resource, &layoutMap, static_cast<uint32>(format), &bindingMap);
				if (!metadata)
				{
					return Output::Failure(metadata.GetError());
				}
			}

			if (!writer.IsValid())
			{
				return Output::Failure(AllocationError());
			}
		}

		if (cancellation.IsCancellationRequested())
		{
			return Output::Failure(CancelledError());
		}

		return WrapShaderDescription(Move(writer.GetBytes()), artifact, resource);
	}

	Result<ShaderBuilder> CanonicalizeValidatedDraft(ShaderBuilder artifact, MemoryResource& resource, CancellationToken cancellation)
	{
		using Output = Result<ShaderBuilder>;
		CanonicalArtifactOrder order(resource);
		Result<void> initialized = order.Initialize(artifact, cancellation);
		if (!initialized)
		{
			return Output::Failure(initialized.GetError());
		}

		for (uint32 variantIndex = 0; variantIndex < artifact.Variants.Size(); ++variantIndex)
		{
			if (cancellation.IsCancellationRequested())
			{
				return Output::Failure(CancelledError());
			}

			ShaderVariant& variant = artifact.Variants[variantIndex];
			const CanonicalVariantOrder& variantOrder = order.GetVariantOrder(variantIndex);
			Result<void> metadata = CanonicalizeVariantMetadata(variant, resource, variantOrder.LayoutMap);
			if (!metadata)
			{
				return Output::Failure(metadata.GetError());
			}

			Array<ShaderDataLayout> layouts(variant.Layouts.GetResource());
			if (!layouts.Reserve(variantOrder.Layouts.Size()))
			{
				return Output::Failure(AllocationError());
			}

			for (uint32 layoutIndex : variantOrder.Layouts)
			{
				ShaderDataLayout& layout = variant.Layouts[layoutIndex];
				Array<uint32> fieldOrder(resource);
				Array<ShaderDataField> fields(layout.Fields.GetResource());
				if (!OrderIndices(layout.Fields, fieldOrder, FieldLess) || !fields.Reserve(layout.Fields.Size()))
				{
					return Output::Failure(AllocationError());
				}

				for (uint32 fieldIndex : fieldOrder)
				{
					fields.AddRequired(Move(layout.Fields[fieldIndex]));
				}

				layout.Fields = Move(fields);
				for (ShaderDataField& field : layout.Fields)
				{
					if (field.NestedLayout != NO_DATA_LAYOUT)
					{
						field.NestedLayout = variantOrder.LayoutMap[field.NestedLayout];
					}
				}

				Result<Hash256> identity = ComputeShaderLayoutIdentity(layout, resource);
				if (!identity)
				{
					return Output::Failure(identity.GetError());
				}

				layout.Identity = identity.Value();
				layouts.AddRequired(Move(layout));
			}

			variant.Layouts = Move(layouts);
			for (ShaderBinding& binding : variant.Bindings)
			{
				if (binding.DataLayout != NO_DATA_LAYOUT)
				{
					binding.DataLayout = variantOrder.LayoutMap[binding.DataLayout];
				}
			}

			if (variant.LocalRecord.DataLayout != NO_DATA_LAYOUT)
			{
				variant.LocalRecord.DataLayout = variantOrder.LayoutMap[variant.LocalRecord.DataLayout];
			}

			Array<ShaderBinding> bindings(variant.Bindings.GetResource());
			if (!bindings.Reserve(variant.Bindings.Size()))
			{
				return Output::Failure(AllocationError());
			}

			for (uint32 bindingIndex : variantOrder.Bindings)
			{
				bindings.AddRequired(Move(variant.Bindings[bindingIndex]));
			}

			variant.Bindings = Move(bindings);
			for (ShaderResourceUse& use : variant.ResourceUses)
			{
				use.BindingIndex = variantOrder.BindingMap[use.BindingIndex];
			}

			for (ShaderLocalRootArgument& argument : variant.LocalRootArguments)
			{
				argument.BindingIndex = variantOrder.BindingMap[argument.BindingIndex];
			}

			if (variant.ResourceUses.Size() > 1)
			{
				Sort(variant.ResourceUses.begin(), variant.ResourceUses.end(), [](const ShaderResourceUse& left, const ShaderResourceUse& right) { return left.BindingIndex < right.BindingIndex; });
			}

			variant.EntryPointIndex = order.GetEntryMap()[variant.EntryPointIndex];
		}

		Array<ShaderEntryPoint> entries(artifact.EntryPoints.GetResource());
		Array<ShaderVariant> variants(artifact.Variants.GetResource());
		if (!entries.Reserve(artifact.EntryPoints.Size()) || !variants.Reserve(artifact.Variants.Size()))
		{
			return Output::Failure(AllocationError());
		}

		for (uint32 entryIndex : order.GetEntries())
		{
			entries.AddRequired(Move(artifact.EntryPoints[entryIndex]));
		}

		for (uint32 variantIndex : order.GetVariants())
		{
			variants.AddRequired(Move(artifact.Variants[variantIndex]));
		}

		artifact.EntryPoints = Move(entries);
		artifact.Variants = Move(variants);
		if (cancellation.IsCancellationRequested())
		{
			return Output::Failure(CancelledError());
		}

		return Output::Success(Move(artifact));
	}

	Result<ShaderBuilder> DecodeShaderArtifactDraft(Span<const Byte> bytes, MemoryResource& resource, const ShaderLimits& limits, CancellationToken cancellation)
	{
		using Output = Result<ShaderBuilder>;
		if (cancellation.IsCancellationRequested())
		{
			return Output::Failure(CancelledError());
		}

		if (bytes.Size() > limits.MaximumTotalBytes || bytes.Size() > 64u * 1024u * 1024u || bytes.Size() < 60 || limits.MaximumNestingDepth > 32 ||
			limits.MaximumTotalBytes > 64u * 1024u * 1024u ||
			limits.MaximumStageIO > 4096 ||
			limits.MaximumPushConstantRanges > 256 ||
			limits.MaximumSpecializationParameters > 1024 ||
			limits.MaximumPayloadTypes > 4096 ||
			limits.MaximumPayloadMembers > 16384 ||
			limits.MaximumPayloadNestingDepth > 16 ||
			limits.MaximumRayDataSignatures > 1024)
		{
			return Output::Failure(MakeArtifactError("Shader artifact byte length is invalid."));
		}

		Reader reader(bytes);
		Span<const Byte> magic = reader.Raw(8);
		const uint32 format = reader.U32();
		if (format != SHADER_FORMAT_VERSION || CompareBytes(magic.Data(), "RXSHADER", 8) != 0)
		{
			return Output::Failure(MakeArtifactError("Unsupported shader cache contract; recook from source.", EErrorCode::Unsupported));
		}

		ShaderBuilder artifact(resource);
		artifact.AbiVersion = reader.U32();
		if (artifact.AbiVersion != format || reader.U32() != 0)
		{
			return Output::Failure(MakeArtifactError("Nonzero reserved shader format field."));
		}

		reader.Hash(artifact.BuildProvenance);
		const uint32 entryCount = reader.U32();
		const uint32 variantCount = reader.U32();
		uint64 totalEntries = 0;
		uint64 totalVariants = 0;
		if (!reader.Count(entryCount, totalEntries, limits.MaximumEntryPoints, 21) || !reader.Count(variantCount, totalVariants, limits.MaximumVariants, 73) ||
			!artifact.EntryPoints.Reserve(entryCount) ||
			!artifact.Variants.Reserve(variantCount))
		{
			return Output::Failure(reader.IsValid() ? AllocationError()
				: MakeArtifactError("Invalid shader entry or variant count."));
		}

		for (uint32 i = 0; i < entryCount && reader.IsValid(); ++i)
		{
			ShaderEntryPoint entry(resource);
			reader.Text(entry.Name, limits.MaximumNameBytes);
			entry.Stage = reader.Enum<EShaderStage>();
			for (uint32& size : entry.ThreadGroupSize)
			{
				size = reader.U32();
			}

			if (!artifact.EntryPoints.Add(Move(entry)))
			{
				return Output::Failure(AllocationError());
			}
		}

		uint64 totalStageIO = 0;
		uint64 totalPushConstants = 0;
		uint64 totalSpecializationParameters = 0;
		uint64 totalRayDataSignatures = 0;
		uint64 totalPayloadTypes = 0;
		uint64 totalPayloadMembers = 0;
		for (uint32 variantIndex = 0; variantIndex < variantCount && reader.IsValid(); ++variantIndex)
		{
			if (cancellation.IsCancellationRequested())
			{
				return Output::Failure(CancelledError());
			}

			ShaderVariant variant(resource);
			variant.Target = ReadProfile(reader);
			variant.EntryPointIndex = reader.U32();
			reader.Text(variant.EmittedEntryPoint, limits.MaximumNameBytes);
			variant.RequiredCapabilities = reader.U64();
			const uint32 codeCount = reader.U32();
			if (!reader.IsValid() || codeCount > limits.MaximumCodeBytes || !reader.GetCursor().CanRead(codeCount))
			{
				return Output::Failure(MakeArtifactError("Invalid shader code length."));
			}

			Span<const Byte> code = reader.Raw(codeCount);
			if (!variant.Bytecode.ResizeUninitialized(codeCount))
			{
				return Output::Failure(AllocationError());
			}

			if (codeCount != 0)
			{
				CopyBytes(variant.Bytecode.Data(), code.Data(), codeCount);
			}

			const uint32 layoutCount = reader.U32();
			const uint32 bindingCount = reader.U32();
			const uint32 useCount = reader.U32();
			uint64 useTotal = 0;
			if (!reader.CountLayouts(layoutCount, limits.MaximumLayouts) || !reader.CountBindings(bindingCount, limits.MaximumBindings) ||
				!reader.Count(useCount, useTotal, bindingCount, 8))
			{
				return Output::Failure(MakeArtifactError("Invalid shader reflection count."));
			}

			if (!variant.Layouts.Reserve(layoutCount) || !variant.Bindings.Reserve(bindingCount) || !variant.ResourceUses.Reserve(useCount))
			{
				return Output::Failure(AllocationError());
			}

			for (uint32 layoutIndex = 0; layoutIndex < layoutCount && reader.IsValid(); ++layoutIndex)
			{
				ShaderDataLayout layout(resource);
				reader.Hash(layout.Identity);
				layout.ByteSize = reader.U64();
				layout.Alignment = reader.U32();
				layout.ElementStride = reader.U64();
				const uint32 fields = reader.U32();
				if (!reader.CountFields(fields, limits.MaximumFields))
				{
					return Output::Failure(MakeArtifactError("Invalid shader field count."));
				}

				if (!layout.Fields.Reserve(fields))
				{
					return Output::Failure(AllocationError());
				}

				for (uint32 fieldIndex = 0; fieldIndex < fields && reader.IsValid(); ++fieldIndex)
				{
					ShaderDataField field(resource);
					readField(reader, field, limits);
					if (!layout.Fields.Add(Move(field)))
					{
						return Output::Failure(AllocationError());
					}
				}

				Result<Hash256> identity = ComputeShaderLayoutIdentity(layout, resource);
				if (!identity)
				{
					return Output::Failure(identity.GetError());
				}

				if (identity.Value() != layout.Identity)
				{
					return Output::Failure(MakeArtifactError("Shader layout digest does not match its canonical fields."));
				}

				if (!variant.Layouts.Add(Move(layout)))
				{
					return Output::Failure(AllocationError());
				}
			}

			for (uint32 bindingIndex = 0; bindingIndex < bindingCount && reader.IsValid(); ++bindingIndex)
			{
				ShaderBinding binding(resource);
				reader.Text(binding.Name, limits.MaximumNameBytes);
				binding.Kind = reader.Enum<EShaderBindingKind>();
				binding.Space = reader.U32();
				binding.Binding = reader.U32();
				binding.RegisterNamespace = reader.Enum<EShaderRegisterClass>();
				binding.ArrayCount = reader.U32();
				binding.DataLayout = reader.U32();
				binding.TextureShape = reader.Enum<EShaderTextureShape>();
				const uint32 array = reader.U32();
				const uint32 multi = reader.U32();
				binding.Access = reader.Enum<EShaderResourceAccess>();
				{
					binding.CountMode = reader.Enum<EShaderDescriptorCountMode>();
				}
				{
					binding.Scope = reader.Enum<EShaderBindingScope>();
				}

				if (array > 1 || multi > 1)
				{
					return Output::Failure(MakeArtifactError("Invalid shader boolean value."));
				}

				binding.bTextureArray = array != 0;
				binding.bMultisampled = multi != 0;
				if (!variant.Bindings.Add(Move(binding)))
				{
					return Output::Failure(AllocationError());
				}
			}

			for (uint32 useIndex = 0; useIndex < useCount && reader.IsValid(); ++useIndex)
			{
				ShaderResourceUse use;
				use.BindingIndex = reader.U32();
				use.Access = reader.Enum<EShaderResourceAccess>();
				if (!variant.ResourceUses.Add(use))
				{
					return Output::Failure(AllocationError());
				}
			}
			{
				Result<void> metadata = ReadVariantMetadata(reader, variant, limits, totalStageIO, totalPushConstants, totalSpecializationParameters, format, totalPayloadTypes, totalPayloadMembers, totalRayDataSignatures);
				if (!metadata)
				{
					return Output::Failure(metadata.GetError());
				}
			}

			if (!artifact.Variants.Add(Move(variant)))
			{
				return Output::Failure(AllocationError());
			}
		}

		if (!reader.IsValid() || reader.GetCursor().GetRemaining() != 0)
		{
			return Output::Failure(reader.HasAllocationFailure() ? AllocationError()
				: MakeArtifactError("Truncated, malformed or trailing shader bytes."));
		}

		Result<void> valid = artifact.Validate(limits);
		if (!valid)
		{
			return Output::Failure(valid.GetError());
		}

		CanonicalArtifactOrder order(resource);
		Result<void> initialized = order.Initialize(artifact, cancellation);
		if (!initialized)
		{
			return Output::Failure(initialized.GetError());
		}

		if (!order.IsCanonical(artifact))
		{
			return Output::Failure(MakeArtifactError("Shader artifact ordering is not canonical."));
		}

		if (cancellation.IsCancellationRequested())
		{
			return Output::Failure(CancelledError());
		}

		return Output::Success(Move(artifact));
	}

	Result<Hash256> ComputeShaderIdentity(const ShaderBuilder& artifact, MemoryResource& resource)
	{
		Result<Array<Byte>> encoded = SaveShader(artifact, resource);
		if (!encoded)
		{
			return Result<Hash256>::Failure(encoded.GetError());
		}

		return Result<Hash256>::Success(ComputeSha256({encoded.Value().Data(), encoded.Value().Size()}));
	}
} // namespace rox::shader
