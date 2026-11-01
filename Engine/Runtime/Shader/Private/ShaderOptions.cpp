#include <ShaderOptions.h>

#include "ShaderMetadata.h"
#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <Shader.h>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		template <class T>
		Result<void> append(Array<T>& array, T value)
		{
			if (!array.Add(Move(value)))
			{
				return Result<void>::Failure(AllocationError());
			}

			return Result<void>::Success();
		}
	}

	Result<void> ShaderOptions::SetKeyword(StringView name, bool bEnabled)
	{
		if (name.IsEmpty() || name.Size() > 4096 || name.Contains('\0') || !IsValidUtf8(name))
		{
			return Result<void>::Failure(MakeArtifactError("Invalid keyword name."));
		}
		for (usize i = 0; i < m_Keywords.Size(); ++i)
		{
			if (m_Keywords[i] != name)
			{
				continue;
			}
			if (!bEnabled)
			{
				m_Keywords.RemoveAt(i);
			}

			return Result<void>::Success();
		}
		if (!bEnabled)
		{
			return Result<void>::Success();
		}
		if (m_Keywords.Size() >= 64)
		{
			return Result<void>::Failure(MakeArtifactError("Too many enabled keywords."));
		}

		String key(m_Keywords.GetResource());
		if (!key.Assign(name))
		{
			return Result<void>::Failure(AllocationError());
		}

		return append(m_Keywords, Move(key));
	}

	Result<void> ShaderOptions::SetPipelineConstant(ShaderSpecializationValue value)
	{
		for (ShaderSpecializationValue& current : m_Constants)
		{
			if (current.ID != value.ID)
			{
				continue;
			}
			current = value;
			return Result<void>::Success();
		}
		if (m_Constants.Size() >= 1024)
		{
			return Result<void>::Failure(MakeArtifactError("Too many pipeline constants."));
		}

		return append(m_Constants, value);
	}

	Result<void> ShaderOptions::SetPipelineConstant(uint32 id, uint32 value)
	{
		return SetPipelineConstant({id, EShaderScalar::UnsignedInteger, 32, value});
	}

	Result<void> ShaderOptions::SetPipelineConstant(uint32 id, int32 value)
	{
		return SetPipelineConstant({id, EShaderScalar::SignedInteger, 32, static_cast<uint32>(value)});
	}

	Result<void> ShaderOptions::SetPipelineConstant(uint32 id, float32 value)
	{
		uint32 bits = 0;
		CopyBytes(&bits, &value, 4);
		return SetPipelineConstant({id, EShaderScalar::Float, 32, bits});
	}

	Result<void> ShaderOptions::SetPipelineConstant(uint32 id, bool bValue)
	{
		return SetPipelineConstant({id, EShaderScalar::Bool, 32, bValue ? 1u : 0u});
	}

	struct ShaderPipelineConstants::Storage
	{
		ShaderLayout Interface;

		Array<ShaderSpecializationValue> Values;

		Hash256 Identity;

		uint32 ThreadGroupSize[3] = {};

		ShaderFeatureRequirements Requirements;

		explicit Storage(MemoryResource& resource) noexcept
			: Values(resource)
		{
		}
	};

	bool ShaderPipelineConstants::IsValid() const noexcept
	{
		return static_cast<bool>(m_Storage);
	}

	const ShaderLayout& ShaderPipelineConstants::GetLayout() const noexcept
	{
		ASSERT(IsValid(), "Resolved specialization must be valid.");
		return m_Storage->Interface;
	}

	Span<const ShaderSpecializationValue> ShaderPipelineConstants::GetValues() const noexcept
	{
		ASSERT(IsValid(), "Resolved specialization must be valid.");
		return m_Storage->Values.View();
	}

	Hash256 ShaderPipelineConstants::GetIdentity() const noexcept
	{
		ASSERT(IsValid(), "Resolved specialization must be valid.");
		return m_Storage->Identity;
	}

	Span<const uint32> ShaderPipelineConstants::GetThreadGroupSize() const noexcept
	{
		ASSERT(IsValid(), "Resolved specialization must be valid.");
		return {m_Storage->ThreadGroupSize, 3};
	}

	ShaderFeatureRequirements ShaderPipelineConstants::GetFeatureRequirements() const noexcept
	{
		ASSERT(IsValid(), "Resolved specialization must be valid.");
		return m_Storage->Requirements;
	}

	Result<ShaderPipelineConstants> ShaderPipelineConstants::Resolve(const ShaderLayout& interface, Span<const ShaderSpecializationValue> values, MemoryResource& resource)
	{
		using R = Result<ShaderPipelineConstants>;
		if (!interface.IsValid() || values.Size() > 1024)
		{
			return R::Failure(MakeArtifactError("Invalid specialization interface or value count."));
		}

		const Span<const ShaderSpecializationParameter> parameters = interface.GetSpecializationParameters();
		ShaderPipelineConstants result;
		if (!MakeSharedInto(resource, &result.m_Storage, resource))
		{
			return R::Failure(AllocationError());
		}

		Storage& storage = *result.m_Storage;
		storage.Interface = interface;
		storage.Requirements = interface.GetFeatureRequirements();
		bool bCompute = false;
		for (const ShaderEntrySelection& selection : interface.GetSelections())
		{
			const ShaderVariant& variant = selection.Artifact.GetVariants()[selection.VariantIndex];
			const ShaderEntryPoint& entry = selection.Artifact.GetEntryPoints()[variant.EntryPointIndex];
			if (entry.Stage == EShaderStage::Compute)
			{
				bCompute = true;
				CopyN(entry.ThreadGroupSize, 3, storage.ThreadGroupSize);
			}
		}

		for (const ShaderSpecializationValue& value : values)
		{
			if (!storage.Values.Add(value))
			{
				return R::Failure(AllocationError());
			}
		}

		if (storage.Values.Size() > 1)
		{
			Sort(storage.Values.begin(), storage.Values.end(), [](const ShaderSpecializationValue& a, const ShaderSpecializationValue& b) { return a.ID < b.ID; });
		}

		for (usize index = 0; index < storage.Values.Size(); ++index)
		{
			const ShaderSpecializationValue& value = storage.Values[index];
			if ((index != 0 && storage.Values[index - 1].ID == value.ID) || parameters.IsEmpty())
			{
				return R::Failure(MakeArtifactError("Duplicate or unknown specialization value."));
			}

			const ShaderSpecializationParameter* const found = LowerBound(parameters.begin(), parameters.end(), value.ID, [](const ShaderSpecializationParameter& p, uint32 id) { return p.ID < id; });
			if (found == parameters.end() || found->ID != value.ID || found->Scalar != value.Scalar || found->ScalarWidth != value.ScalarWidth ||
				!SpecializationValueValid(*found, value.Bits))
			{
				return R::Failure(MakeArtifactError("Unknown, mistyped or out-of-domain specialization value."));
			}
		}

		Array<ShaderSpecializationValue> canonical(resource);
		if (!canonical.Reserve(parameters.Size()))
		{
			return R::Failure(AllocationError());
		}

		for (const ShaderSpecializationParameter& parameter : parameters)
		{
			if (parameter.Phase != EShaderSpecializationPhase::PipelineTime || parameter.bAffectsLayout)
			{
				return R::Failure(MakeArtifactError("Unresolved build-time or layout-affecting specialization requires emitted compatible code and interface.", EErrorCode::Unsupported));
			}

			ShaderSpecializationValue value;
			value.ID = parameter.ID;
			value.Scalar = parameter.Scalar;
			value.ScalarWidth = parameter.ScalarWidth;
			ShaderSpecializationValue* found = storage.Values.end();
			if (!storage.Values.IsEmpty())
			{
				found = LowerBound(storage.Values.begin(), storage.Values.end(), parameter.ID,
					[](const ShaderSpecializationValue& candidate, uint32 id) { return candidate.ID < id; });
			}

			if (found != storage.Values.end() && found->ID == parameter.ID)
			{
				value.Bits = found->Bits;
			}
			else if (parameter.bHasDefault)
			{
				value.Bits = parameter.DefaultBits;
			}
			else
			{
				return R::Failure(MakeArtifactError("Required specialization value is missing."));
			}

			if (parameter.WorkgroupAxis != NO_STAGE_LOCATION)
			{
				if (value.Bits == 0 || value.Bits > 65535)
				{
					return R::Failure(MakeArtifactError("Specialized workgroup dimension exceeds its portable bound."));
				}

				storage.ThreadGroupSize[parameter.WorkgroupAxis] = static_cast<uint32>(value.Bits);
			}

			canonical.AddRequired(value);
		}

		storage.Values = Move(canonical);
		const uint64 workgroup = uint64(storage.ThreadGroupSize[0]) * storage.ThreadGroupSize[1] * storage.ThreadGroupSize[2];
		if (workgroup > 1024)
		{
			return R::Failure(MakeArtifactError("Specialized workgroup invocation product exceeds its portable bound."));
		}

		if (bCompute)
		{
			storage.Requirements.MinimumWorkgroupInvocations = static_cast<uint32>(workgroup);
		}
		Writer identity(resource, true);
		identity.Raw("ROX-SHADER-SPECIALIZATION", 25);
		identity.U32(1);
		identity.Hash(interface.GetLayoutHash());
		identity.U32(static_cast<uint32>(storage.Values.Size()));
		for (const ShaderSpecializationValue& value : storage.Values)
		{
			identity.U32(value.ID);
			identity.Enum(value.Scalar);
			identity.U32(value.ScalarWidth);
			identity.U64(value.Bits);
		}

		for (uint32 dimension : storage.ThreadGroupSize)
		{
			identity.U32(dimension);
		}

		if (!identity.IsValid())
		{
			return R::Failure(AllocationError());
		}

		storage.Identity = identity.GetHash();
		return R::Success(Move(result));
	}

	ShaderPipelineConstants::ShaderPipelineConstants() noexcept = default;
	ShaderPipelineConstants::~ShaderPipelineConstants() = default;
	ShaderPipelineConstants::ShaderPipelineConstants(const ShaderPipelineConstants&) noexcept = default;
	ShaderPipelineConstants::ShaderPipelineConstants(ShaderPipelineConstants&&) noexcept = default;
	ShaderPipelineConstants& ShaderPipelineConstants::operator=(const ShaderPipelineConstants&) noexcept = default;
	ShaderPipelineConstants& ShaderPipelineConstants::operator=(ShaderPipelineConstants&&) noexcept = default;
} // namespace rox::shader
