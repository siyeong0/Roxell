#include "ShaderRayMetadata.h"

#include "ShaderPayloadCanonicalizer.h"
#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <Types.h>
#include <Utility.hpp>
#include <Vocabulary.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		bool isRaySignatureLess(const ShaderRayDataSignature& left, const ShaderRayDataSignature& right) noexcept
		{
			return Tie(left.Kind, left.Direction, left.Location) < Tie(right.Kind, right.Direction, right.Location);
		}

		Result<void> makeRayMetadataFailure(StringView message)
		{
			return Result<void>::Failure(MakeArtifactError(message));
		}
	} // namespace

	bool IsRayStage(EShaderStage stage) noexcept
	{
		return stage >= EShaderStage::RayGeneration && stage <= EShaderStage::Callable;
	}

	bool IsDefaultRayInterface(const ShaderRayInterface& ray) noexcept
	{
		return ray.DeclaredLimits == ShaderRayDeclaredLimits{} && ray.Data.IsEmpty() && !ray.bOutgoingDataComplete;
	}

	Result<void> ValidateRayInterface(const ShaderRayInterface& ray, const ShaderEntryPoint& entry, EShaderTarget target, const ShaderLimits& limits, uint64& totalBytes)
	{
		const bool bRequiresCompleteOutgoingData = target == EShaderTarget::SpirV;
		if (!IsRayStage(entry.Stage) || ray.Data.Size() > 64 || ray.Data.Size() > limits.MaximumRayDataSignatures || ray.bOutgoingDataComplete != bRequiresCompleteOutgoingData)
		{
			return makeRayMetadataFailure("Ray reflection stage, completeness or record bounds are invalid.");
		}

		totalBytes += 20;
		for (usize index = 0; index < ray.Data.Size(); ++index)
		{
			const ShaderRayDataSignature& data = ray.Data[index];
			const bool bInvalidSizeProof = data.bHasProvenByteSize != (data.ProvenByteSize != 0);
			const bool bInvalidDxilLocation = target == EShaderTarget::Dxil &&
				(data.Direction != EShaderRayDataDirection::Incoming || data.Location != NO_STAGE_LOCATION);
			if (data.Kind > EShaderRayDataKind::Callable || data.Direction > EShaderRayDataDirection::Outgoing || bInvalidDxilLocation || bInvalidSizeProof ||
				(target == EShaderTarget::SpirV && data.bHasProvenByteSize))
			{
				return makeRayMetadataFailure("Ray data role, target location or native size proof is invalid.");
			}

			if (data.Direction == EShaderRayDataDirection::Incoming)
			{
				bool bStageValid = false;
				switch (data.Kind)
				{
				case EShaderRayDataKind::Payload:
					bStageValid = entry.Stage == EShaderStage::Miss ||
						entry.Stage == EShaderStage::ClosestHit || entry.Stage == EShaderStage::AnyHit;
					break;

				case EShaderRayDataKind::HitAttributes:
					bStageValid = entry.Stage == EShaderStage::ClosestHit || entry.Stage == EShaderStage::AnyHit;
					break;

				default:
					bStageValid = entry.Stage == EShaderStage::Callable;
					break;
				}

				if (!bStageValid)
				{
					return makeRayMetadataFailure("Incoming ray data is incompatible with its selected export stage.");
				}
			}
			else
			{
				const bool bPayloadStage = entry.Stage == EShaderStage::RayGeneration ||
					entry.Stage == EShaderStage::ClosestHit || entry.Stage == EShaderStage::Miss;
				bool bStageValid = bPayloadStage || entry.Stage == EShaderStage::Callable;
				if (data.Kind == EShaderRayDataKind::HitAttributes)
				{
					bStageValid = entry.Stage == EShaderStage::Intersection;
				}
				else if (data.Kind == EShaderRayDataKind::Payload)
				{
					bStageValid = bPayloadStage;
				}

				if (!bStageValid)
				{
					return makeRayMetadataFailure("Outgoing ray data is incompatible with its selected export stage.");
				}
			}

			for (usize prior = 0; prior < index; ++prior)
			{
				const ShaderRayDataSignature& other = ray.Data[prior];
				if (!isRaySignatureLess(data, other) && !isRaySignatureLess(other, data))
				{
					return makeRayMetadataFailure("Duplicate ray data role and native location.");
				}
			}

			ShaderPayloadCanonicalizer shape(data.Types.View(), data.RootType, data.Types.GetResource());
			Result<void> validated = shape.ValidateStructure(limits);
			if (!validated)
			{
				return validated;
			}

			uint32 declared = ray.DeclaredLimits.MaximumCallableBytes;
			if (data.Kind == EShaderRayDataKind::Payload)
			{
				declared = ray.DeclaredLimits.MaximumPayloadBytes;
			}
			else if (data.Kind == EShaderRayDataKind::HitAttributes)
			{
				declared = ray.DeclaredLimits.MaximumAttributeBytes;
			}

			const uint32 minimumScalarBytes = shape.GetMinimumScalarBytes();
			if (declared < minimumScalarBytes || (data.bHasProvenByteSize && (data.ProvenByteSize > declared || data.ProvenByteSize < minimumScalarBytes)))
			{
				return makeRayMetadataFailure("Declared ray data bounds are smaller than actual represented requirements.");
			}

			totalBytes += 28 + 24 * data.Types.Size();
			for (const ShaderPayloadType& type : data.Types)
			{
				totalBytes += 4 * type.MemberTypes.Size();
			}
		}

		return Result<void>::Success();
	}

	Result<void> CloneRayInterface(const ShaderRayInterface& source, ShaderRayInterface& result)
	{
		result.DeclaredLimits = source.DeclaredLimits;
		result.bOutgoingDataComplete = source.bOutgoingDataComplete;
		if (!result.Data.Reserve(source.Data.Size()))
		{
			return Result<void>::Failure(AllocationError());
		}

		for (const ShaderRayDataSignature& value : source.Data)
		{
			ShaderRayDataSignature data(result.Data.GetResource());
			data.Kind = value.Kind;
			data.Direction = value.Direction;
			data.Location = value.Location;
			data.RootType = value.RootType;
			data.bHasProvenByteSize = value.bHasProvenByteSize;
			data.ProvenByteSize = value.ProvenByteSize;

			for (const ShaderPayloadType& sourceType : value.Types)
			{
				ShaderPayloadType type(result.Data.GetResource());
				type.Kind = sourceType.Kind;
				type.Scalar = sourceType.Scalar;
				type.ScalarWidth = sourceType.ScalarWidth;
				type.Count = sourceType.Count;
				type.ElementType = sourceType.ElementType;
				if (!type.MemberTypes.Reserve(sourceType.MemberTypes.Size()))
				{
					return Result<void>::Failure(AllocationError());
				}

				for (uint32 member : sourceType.MemberTypes)
				{
					type.MemberTypes.AddRequired(member);
				}

				if (!data.Types.Add(Move(type)))
				{
					return Result<void>::Failure(AllocationError());
				}
			}

			result.Data.AddRequired(Move(data));
		}

		return Result<void>::Success();
	}

	Result<void> CanonicalizeRayInterface(ShaderRayInterface& ray)
	{
		for (ShaderRayDataSignature& data : ray.Data)
		{
			ShaderPayloadCanonicalizer shape(data.Types.View(), data.RootType, data.Types.GetResource());
			Array<ShaderPayloadType> types(data.Types.GetResource());
			uint32 root = NO_PAYLOAD_TYPE;
			Result<void> canonical = shape.CanonicalizeStructure(types, root);
			if (!canonical)
			{
				return canonical;
			}

			data.Types = Move(types);
			data.RootType = root;
		}

		if (ray.Data.Size() > 1)
		{
			Sort(ray.Data.begin(), ray.Data.end(), isRaySignatureLess);
		}

		return Result<void>::Success();
	}

	bool IsCanonicalRayInterface(const ShaderRayInterface& ray) noexcept
	{
		for (usize index = 0; index < ray.Data.Size(); ++index)
		{
			const ShaderRayDataSignature& data = ray.Data[index];
			if ((index != 0 && !isRaySignatureLess(ray.Data[index - 1], data)) || !ShaderPayloadCanonicalizer::IsCanonicalStructure(data.Types.View(), data.RootType))
			{
				return false;
			}
		}

		return true;
	}

	Result<void> WriteRayInterface(Writer& writer, const ShaderRayInterface& source, MemoryResource& resource, bool bCanonical)
	{
		ShaderRayInterface ordered(resource);
		const ShaderRayInterface* ray = &source;
		if (!bCanonical)
		{
			Result<void> cloned = CloneRayInterface(source, ordered);
			if (!cloned)
			{
				return cloned;
			}

			Result<void> canonical = CanonicalizeRayInterface(ordered);
			if (!canonical)
			{
				return canonical;
			}

			ray = &ordered;
		}

		writer.U32(ray->DeclaredLimits.MaximumPayloadBytes);
		writer.U32(ray->DeclaredLimits.MaximumAttributeBytes);
		writer.U32(ray->DeclaredLimits.MaximumCallableBytes);
		writer.U32(ray->bOutgoingDataComplete ? 1 : 0);
		writer.U32(static_cast<uint32>(ray->Data.Size()));

		for (const ShaderRayDataSignature& data : ray->Data)
		{
			writer.Enum(data.Kind);
			writer.Enum(data.Direction);
			writer.U32(data.Location);
			writer.U32(data.RootType);
			writer.U32(data.bHasProvenByteSize ? 1 : 0);
			writer.U32(data.ProvenByteSize);
			writer.U32(static_cast<uint32>(data.Types.Size()));

			for (const ShaderPayloadType& type : data.Types)
			{
				writer.Enum(type.Kind);
				writer.Enum(type.Scalar);
				writer.U32(type.ScalarWidth);
				writer.U32(type.Count);
				writer.U32(type.ElementType);
				writer.U32(static_cast<uint32>(type.MemberTypes.Size()));
				for (uint32 member : type.MemberTypes)
				{
					writer.U32(member);
				}
			}
		}

		if (!writer.IsValid())
		{
			return Result<void>::Failure(AllocationError());
		}

		return Result<void>::Success();
	}

	Result<void> ReadRayInterface(Reader& reader, ShaderRayInterface& ray, const ShaderLimits& limits, uint64& totalSignatures, uint64& totalTypes, uint64& totalMembers)
	{
		ray.DeclaredLimits.MaximumPayloadBytes = reader.U32();
		ray.DeclaredLimits.MaximumAttributeBytes = reader.U32();
		ray.DeclaredLimits.MaximumCallableBytes = reader.U32();
		const uint32 complete = reader.U32();
		const uint32 count = reader.U32();
		if (complete > 1 || count > 64 || !reader.Count(count, totalSignatures, limits.MaximumRayDataSignatures, 28))
		{
			return makeRayMetadataFailure("Invalid or excessive ray interface records.");
		}

		ray.bOutgoingDataComplete = complete != 0;
		if (!ray.Data.Reserve(count))
		{
			return Result<void>::Failure(AllocationError());
		}

		for (uint32 index = 0; index < count; ++index)
		{
			ShaderRayDataSignature data(ray.Data.GetResource());
			data.Kind = reader.Enum<EShaderRayDataKind>();
			data.Direction = reader.Enum<EShaderRayDataDirection>();
			data.Location = reader.U32();
			data.RootType = reader.U32();
			const uint32 sizeProof = reader.U32();
			data.ProvenByteSize = reader.U32();
			const uint32 types = reader.U32();
			if (sizeProof > 1 || data.Kind > EShaderRayDataKind::Callable || data.Direction > EShaderRayDataDirection::Outgoing || types > 128 ||
				!reader.Count(types, totalTypes, limits.MaximumPayloadTypes, 24))
			{
				return makeRayMetadataFailure("Invalid or excessive ray structural type records.");
			}

			data.bHasProvenByteSize = sizeProof != 0;
			if (!data.Types.Reserve(types))
			{
				return Result<void>::Failure(AllocationError());
			}

			for (uint32 typeIndex = 0; typeIndex < types; ++typeIndex)
			{
				ShaderPayloadType type(ray.Data.GetResource());
				type.Kind = reader.Enum<EShaderPayloadTypeKind>();
				type.Scalar = reader.Enum<EShaderScalar>();
				type.ScalarWidth = reader.U32();
				type.Count = reader.U32();
				type.ElementType = reader.U32();
				const uint32 members = reader.U32();
				if (members > 32 || !reader.Count(members, totalMembers, limits.MaximumPayloadMembers, 4))
				{
					return makeRayMetadataFailure("Ray structure member count exceeds decoding bounds.");
				}

				if (!type.MemberTypes.Reserve(members))
				{
					return Result<void>::Failure(AllocationError());
				}

				for (uint32 member = 0; member < members; ++member)
				{
					type.MemberTypes.AddRequired(reader.U32());
				}

				data.Types.AddRequired(Move(type));
			}

			ray.Data.AddRequired(Move(data));
		}

		if (!reader.IsValid())
		{
			return makeRayMetadataFailure("Truncated ray interface metadata.");
		}

		return Result<void>::Success();
	}
} // namespace rox::shader
