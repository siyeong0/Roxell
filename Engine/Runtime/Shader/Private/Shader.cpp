#include <Shader.h>

#include "ShaderDescription.h"
#include "ShaderMetadata.h"
#include "ShaderRayMetadata.h"
#include "ShaderSerialization.h"
#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <MemoryOperations.hpp>
#include <ShaderBuilder.h>
#include <UniquePtr.hpp>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	struct Shader::Storage
	{
		ShaderBuilder Draft;

		Hash256 Identity;
		Array<Hash256> CodeHashes;

		explicit Storage(MemoryResource& resource) noexcept
			: Draft(resource)
			, CodeHashes(resource)
		{
		}
	};

	namespace
	{
		Result<ShaderBuilder> cloneDraft(const ShaderBuilder& original, MemoryResource& resource, CancellationToken cancellation)
		{
			using R = Result<ShaderBuilder>;
			ShaderBuilder draft(resource);
			draft.AbiVersion = original.AbiVersion;
			draft.BuildProvenance = original.BuildProvenance;
			for (const ShaderEntryPoint& entry : original.EntryPoints)
			{
				ShaderEntryPoint copy(resource);
				copy.Stage = entry.Stage;
				CopyN(entry.ThreadGroupSize, 3, copy.ThreadGroupSize);
				if (!copy.Name.Assign(entry.Name) || !draft.EntryPoints.Add(Move(copy)))
				{
					return R::Failure(AllocationError());
				}
			}

			for (const ShaderVariant& variant : original.Variants)
			{
				if (cancellation.IsCancellationRequested())
				{
					return R::Failure(CancelledError());
				}

				ShaderVariant copy(resource);
				copy.Target = variant.Target;
				copy.EntryPointIndex = variant.EntryPointIndex;
				copy.RequiredCapabilities = variant.RequiredCapabilities;
				copy.Requirements = variant.Requirements;
				copy.LocalRecord = variant.LocalRecord;
				for (const ShaderLocalRootArgument& argument : variant.LocalRootArguments)
				{
					if (!copy.LocalRootArguments.Add(argument))
					{
						return R::Failure(AllocationError());
					}
				}

				copy.bStageIOComplete = variant.bStageIOComplete;
				copy.MeshOutput = variant.MeshOutput;
				Result<void> ray = CloneRayInterface(variant.RayInterface, copy.RayInterface);
				if (!ray)
				{
					return R::Failure(Move(ray.GetError()));
				}

				copy.TaskPayload.Proof = variant.TaskPayload.Proof;
				copy.TaskPayload.RootType = variant.TaskPayload.RootType;
				copy.TaskPayload.bHasProvenByteSize = variant.TaskPayload.bHasProvenByteSize;
				copy.TaskPayload.ProvenByteSize = variant.TaskPayload.ProvenByteSize;
				for (const ShaderPayloadType& type : variant.TaskPayload.Types)
				{
					ShaderPayloadType record(resource);
					record.Kind = type.Kind;
					record.Scalar = type.Scalar;
					record.ScalarWidth = type.ScalarWidth;
					record.Count = type.Count;
					record.ElementType = type.ElementType;
					if (!record.MemberTypes.Reserve(type.MemberTypes.Size()))
					{
						return R::Failure(AllocationError());
					}

					for (uint32 member : type.MemberTypes)
					{
						record.MemberTypes.AddRequired(member);
					}

					if (!copy.TaskPayload.Types.Add(Move(record)))
					{
						return R::Failure(AllocationError());
					}
				}

				if (!copy.EmittedEntryPoint.Assign(variant.EmittedEntryPoint) || !copy.Bytecode.ResizeUninitialized(variant.Bytecode.Size()))
				{
					return R::Failure(AllocationError());
				}

				if (!variant.Bytecode.IsEmpty())
				{
					CopyBytes(copy.Bytecode.Data(), variant.Bytecode.Data(), variant.Bytecode.Size());
				}

				for (const ShaderDataLayout& layout : variant.Layouts)
				{
					ShaderDataLayout layoutCopy(resource);
					layoutCopy.ByteSize = layout.ByteSize;
					layoutCopy.Alignment = layout.Alignment;
					layoutCopy.ElementStride = layout.ElementStride;
					layoutCopy.Identity = layout.Identity;
					for (const ShaderDataField& field : layout.Fields)
					{
						ShaderDataField fieldCopy(resource);
						fieldCopy.ByteOffset = field.ByteOffset;
						fieldCopy.ByteSize = field.ByteSize;
						fieldCopy.Scalar = field.Scalar;
						fieldCopy.ScalarWidth = field.ScalarWidth;
						fieldCopy.VectorWidth = field.VectorWidth;
						fieldCopy.MatrixRows = field.MatrixRows;
						fieldCopy.MatrixColumns = field.MatrixColumns;
						fieldCopy.MatrixOrder = field.MatrixOrder;
						fieldCopy.MatrixStride = field.MatrixStride;
						fieldCopy.ArrayCount = field.ArrayCount;
						fieldCopy.ArrayStride = field.ArrayStride;
						fieldCopy.NestedLayout = field.NestedLayout;
						if (!fieldCopy.Name.Assign(field.Name) || !layoutCopy.Fields.Add(Move(fieldCopy)))
						{
							return R::Failure(AllocationError());
						}
					}

					if (!copy.Layouts.Add(Move(layoutCopy)))
					{
						return R::Failure(AllocationError());
					}
				}

				for (const ShaderBinding& binding : variant.Bindings)
				{
					ShaderBinding bindingCopy(resource);
					bindingCopy.Kind = binding.Kind;
					bindingCopy.Space = binding.Space;
					bindingCopy.Binding = binding.Binding;
					bindingCopy.RegisterNamespace = binding.RegisterNamespace;
					bindingCopy.ArrayCount = binding.ArrayCount;
					bindingCopy.CountMode = binding.CountMode;
					bindingCopy.Scope = binding.Scope;
					bindingCopy.DataLayout = binding.DataLayout;
					bindingCopy.TextureShape = binding.TextureShape;
					bindingCopy.bTextureArray = binding.bTextureArray;
					bindingCopy.bMultisampled = binding.bMultisampled;
					bindingCopy.Access = binding.Access;
					if (!bindingCopy.Name.Assign(binding.Name) || !copy.Bindings.Add(Move(bindingCopy)))
					{
						return R::Failure(AllocationError());
					}
				}

				for (const ShaderResourceUse& use : variant.ResourceUses)
				{
					if (!copy.ResourceUses.Add(use))
					{
						return R::Failure(AllocationError());
					}
				}

				auto cloneIO = [&](const Array<ShaderStageIO>& source, Array<ShaderStageIO>& destination)
				{
					for (const ShaderStageIO& io : source)
					{
						ShaderStageIO record(resource);
						record.SemanticIndex = io.SemanticIndex;
						record.Location = io.Location;
						record.Component = io.Component;
						record.Rate = io.Rate;
						record.Builtin = io.Builtin;
						record.Scalar = io.Scalar;
						record.ScalarWidth = io.ScalarWidth;
						record.VectorWidth = io.VectorWidth;
						record.ArrayCount = io.ArrayCount;
						record.Interpolation = io.Interpolation;
						record.bCentroid = io.bCentroid;
						record.bSample = io.bSample;
						if (!record.Name.Assign(io.Name) || !record.Semantic.Assign(io.Semantic) || !destination.Add(Move(record)))
						{
							return false;
						}
					}

					return true;
				};
				if (!cloneIO(variant.StageInputs, copy.StageInputs) || !cloneIO(variant.StageOutputs, copy.StageOutputs))
				{
					return R::Failure(AllocationError());
				}

				for (const ShaderPushConstantRange& range : variant.PushConstants)
				{
					if (!copy.PushConstants.Add(range))
					{
						return R::Failure(AllocationError());
					}
				}

				for (const ShaderSpecializationParameter& p : variant.SpecializationParameters)
				{
					Result<ShaderSpecializationParameter> record = CloneSpecializationParameter(p, resource);
					if (!record)
					{
						return R::Failure(record.GetError());
					}

					if (!copy.SpecializationParameters.Add(Move(record).TakeValue()))
					{
						return R::Failure(AllocationError());
					}
				}

				if (!draft.Variants.Add(Move(copy)))
				{
					return R::Failure(AllocationError());
				}
			}

			Result<void> description = CloneDescription(original, draft, {});
			if (!description)
			{
				return R::Failure(description.GetError());
			}

			return R::Success(Move(draft));
		}
	} // namespace

	Result<Shader> Shader::Build(ShaderBuilder draft, MemoryResource& resource, const ShaderLimits& limits, CancellationToken cancellation)
	{
		using R = Result<Shader>;
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(CancelledError());
		}

		Result<void> checked = draft.Validate(limits);
		if (!checked)
		{
			return R::Failure(checked.GetError());
		}

		Result<ShaderBuilder> canonical = CanonicalizeValidatedDraft(Move(draft), resource, cancellation);
		if (!canonical)
		{
			return R::Failure(canonical.GetError());
		}

		return Shader::publishCanonical(Move(canonical).TakeValue(), resource, cancellation, nullptr);
	}

	Result<Shader> Shader::Create(const ShaderBuilder& draft, MemoryResource& resource, const ShaderLimits& limits, CancellationToken cancellation)
	{
		if (cancellation.IsCancellationRequested())
		{
			return Result<Shader>::Failure(CancelledError());
		}

		Result<void> checked = draft.Validate(limits);
		if (!checked)
		{
			return Result<Shader>::Failure(checked.GetError());
		}

		Result<ShaderBuilder> copy = cloneDraft(draft, resource, cancellation);
		if (!copy)
		{
			return Result<Shader>::Failure(copy.GetError());
		}

		Result<ShaderBuilder> canonical = CanonicalizeValidatedDraft(Move(copy).TakeValue(), resource, cancellation);
		if (!canonical)
		{
			return Result<Shader>::Failure(canonical.GetError());
		}

		return Shader::publishCanonical(Move(canonical).TakeValue(), resource, cancellation, nullptr);
	}

	Result<Shader> Shader::Load(Span<const Byte> bytes, MemoryResource& resource, const ShaderLimits& limits, CancellationToken cancellation)
	{
		Result<ShaderBuilder> decoded = LoadShaderDescription(bytes, resource, limits, cancellation);
		if (!decoded)
		{
			return Result<Shader>::Failure(decoded.GetError());
		}

		const Hash256 identity = ComputeSha256({bytes.Data(), bytes.Size()});
		return Shader::publishCanonical(Move(decoded).TakeValue(), resource, cancellation, &identity);
	}

	Shader::Shader() noexcept = default;

	Shader::Shader(const Shader&) noexcept = default;

	Shader::Shader(Shader&&) noexcept = default;

	Shader::~Shader() = default;

	Shader& Shader::operator=(const Shader&) noexcept = default;

	Shader& Shader::operator=(Shader&&) noexcept = default;

	Result<Array<Byte>> Shader::Save(MemoryResource& memory, CancellationToken cancellation) const
	{
		using R = Result<Array<Byte>>;
		if (!IsValid())
		{
			return R::Failure(MakeArtifactError("Invalid shader handle."));
		}

		Writer writer(memory);
		Result<void> written = WriteCanonicalArtifact(writer, m_Storage->Draft, cancellation);
		if (!written)
		{
			return R::Failure(written.GetError());
		}

		Result<Array<Byte>> result = WrapShaderDescription(Move(writer.GetBytes()), m_Storage->Draft, memory);
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(CancelledError());
		}

		return result;
	}

	Result<Array<Byte>> Shader::Save(EShaderFormat format, MemoryResource& memory, CancellationToken token) const
	{
		if (!IsValid())
		{
			return Result<Array<Byte>>::Failure(MakeArtifactError("Invalid shader artifact handle."));
		}

		if (format != EShaderFormat::V1)
		{
			return Result<Array<Byte>>::Failure(MakeArtifactError("Unsupported shader code format."));
		}

		return Save(memory, token);
	}

	StringView Shader::GetName() const noexcept
	{
		return IsValid() ? StringView(m_Storage->Draft.Name) : StringView{};
	}

	Span<const ShaderProperty> Shader::GetProperties() const noexcept
	{
		return IsValid() ? m_Storage->Draft.Properties.View() : Span<const ShaderProperty>{};
	}

	Span<const ShaderPass> Shader::GetPasses() const noexcept
	{
		return IsValid() ? m_Storage->Draft.Passes.View() : Span<const ShaderPass>{};
	}

	Span<const ShaderKernel> Shader::GetKernels() const noexcept
	{
		return IsValid() ? m_Storage->Draft.Kernels.View() : Span<const ShaderKernel>{};
	}

	Span<const String> Shader::GetSupportedOptions() const noexcept
	{
		return IsValid() ? m_Storage->Draft.Keywords.View() : Span<const String>{};
	}

	Span<const ShaderPropertyBinding> Shader::GetPropertyBindings() const noexcept
	{
		return IsValid() ? m_Storage->Draft.PropertyBindings.View() : Span<const ShaderPropertyBinding>{};
	}

	bool Shader::IsValid() const noexcept
	{
		return static_cast<bool>(m_Storage);
	}

	uint32 Shader::GetAbiVersion() const noexcept
	{
		ASSERT(IsValid(), "Shader artifact must be valid.");
		return m_Storage->Draft.AbiVersion;
	}

	Hash256 Shader::GetBuildProvenance() const noexcept
	{
		ASSERT(IsValid(), "Shader artifact must be valid.");
		return m_Storage->Draft.BuildProvenance;
	}

	Hash256 Shader::GetIdentity() const noexcept
	{
		ASSERT(IsValid(), "Shader artifact must be valid.");
		return m_Storage->Identity;
	}

	Hash256 Shader::GetVariantCodeHash(uint32 index) const noexcept
	{
		ASSERT(IsValid(), "Shader artifact must be valid.");
		ASSERT(index < m_Storage->CodeHashes.Size(), "Shader variant code hash requires a valid variant index.");

		return m_Storage->CodeHashes[index];
	}

	Span<const ShaderEntryPoint> Shader::GetEntryPoints() const noexcept
	{
		ASSERT(IsValid(), "Shader artifact must be valid.");
		return m_Storage->Draft.EntryPoints.View();
	}

	Span<const ShaderVariant> Shader::GetVariants() const noexcept
	{
		ASSERT(IsValid(), "Shader artifact must be valid.");
		return m_Storage->Draft.Variants.View();
	}

	Result<uint32> Shader::FindVariant(const ShaderVariantKey& key) const
	{
		using R = Result<uint32>;
		if (!IsValid() || key.EntryPoint.IsEmpty())
		{
			return R::Failure(MakeArtifactError("Invalid shader artifact or variant key."));
		}

		const Span<const ShaderEntryPoint> entries = GetEntryPoints();
		const ShaderEntryPoint* const entry = LowerBound(entries.begin(), entries.end(), key.EntryPoint, [](const ShaderEntryPoint& value, StringView name) { return value.Name.View() < name; });
		if (entry == entries.end() || entry->Name.View() != key.EntryPoint)
		{
			return R::Failure(MakeArtifactError("Shader entry was not found.", EErrorCode::NotFound));
		}

		const uint32 entryIndex = static_cast<uint32>(entry - entries.begin());
		const Span<const ShaderVariant> variants = GetVariants();
		const ShaderVariant* const found = LowerBound(variants.begin(), variants.end(), key, [&](const ShaderVariant& value, const ShaderVariantKey& requested)
			{
				if (value.EntryPointIndex != entryIndex)
				{
					return value.EntryPointIndex < entryIndex;
				}

				return value.Target < requested.Target;
			});
		if (found == variants.end() || found->EntryPointIndex != entryIndex || found->Target != key.Target)
		{
			return R::Failure(MakeArtifactError("Exact shader target variant was not found.", EErrorCode::NotFound));
		}

		return R::Success(static_cast<uint32>(found - variants.begin()));
	}

	Result<Shader> Shader::publishCanonical(ShaderBuilder draft, MemoryResource& resource, CancellationToken cancellation, const Hash256* suppliedIdentity)
	{
		using R = Result<Shader>;
		Shader artifact;
		if (!MakeSharedInto(resource, &artifact.m_Storage, resource))
		{
			return R::Failure(AllocationError());
		}

		// Construct the retained draft in place so moves preserve every original allocation resource.
		DestroyAt(&artifact.m_Storage->Draft);
		ConstructAt(&artifact.m_Storage->Draft, Move(draft));
		if (suppliedIdentity != nullptr)
		{
			artifact.m_Storage->Identity = *suppliedIdentity;
		}
		else if (HasDescription(artifact.m_Storage->Draft))
		{
			Result<Array<Byte>> bytes = artifact.Save(resource, cancellation);
			if (!bytes)
			{
				return R::Failure(bytes.GetError());
			}
			artifact.m_Storage->Identity = ComputeSha256(bytes.Value().View());
		}
		else
		{
			Writer identity(resource, true);
			Result<void> written = WriteCanonicalArtifact(identity, artifact.m_Storage->Draft, cancellation);
			if (!written)
			{
				return R::Failure(written.GetError());
			}

			artifact.m_Storage->Identity = identity.GetHash();
		}

		for (const ShaderVariant& variant : artifact.m_Storage->Draft.Variants)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(CancelledError());
			}

			Writer code(resource, true);
			code.Raw("ROX-SHADER-CODE", 15);
			code.U32(1);
			WriteProfile(code, variant.Target);
			code.U32(static_cast<uint32>(variant.Bytecode.Size()));
			code.Raw(variant.Bytecode.Data(), variant.Bytecode.Size());
			if (!code.IsValid() || !artifact.m_Storage->CodeHashes.Add(code.GetHash()))
			{
				return R::Failure(AllocationError());
			}
		}

		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(CancelledError());
		}

		return R::Success(Move(artifact));
	}

	Result<ShaderBuilder> ShaderBuilder::FromShader(const Shader& shader, MemoryResource& memory)
	{
		if (!shader.IsValid())
		{
			return Result<ShaderBuilder>::Failure(MakeArtifactError("Invalid shader handle."));
		}

		return cloneDraft(shader.m_Storage->Draft, memory, {});
	}

} // namespace rox::shader
