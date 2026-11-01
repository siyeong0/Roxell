#include "ShaderPayloadCanonicalizer.h"

#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <Comparison.hpp>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	ShaderPayloadCanonicalizer::ShaderPayloadCanonicalizer(const ShaderTaskPayloadSignature& signature, MemoryResource& resource) noexcept
		: ShaderPayloadCanonicalizer(signature.Types.View(), signature.RootType, resource)
	{
		m_TaskSignature = &signature;
	}

	ShaderPayloadCanonicalizer::ShaderPayloadCanonicalizer(Span<const ShaderPayloadType> types, uint32 root, MemoryResource& resource) noexcept
		: m_Types(types)
		, m_RootType(root)
		, m_CanonicalTypes(resource)
	{
		for (uint32& index : m_Map)
		{
			index = NO_PAYLOAD_TYPE;
		}
	}

	Result<void> ShaderPayloadCanonicalizer::validateTypeRecursive(uint32 index, uint32 maximumDepth)
	{
		using R = Result<void>;
		if (index >= m_Types.Size() || m_States[index] == 1)
		{
			return R::Failure(MakeArtifactError("Invalid or cyclic task payload type reference."));
		}

		if (m_States[index] == 2)
		{
			return R::Success();
		}

		m_States[index] = 1;
		const ShaderPayloadType& type = m_Types[index];
		uint32 components = 0;
		uint32 depth = 1;
		auto child = [&](uint32 childIndex)
		{
			Result<void> validated = validateTypeRecursive(childIndex, maximumDepth);
			if (!validated)
			{
				return validated;
			}

			if (m_Depths[childIndex] >= maximumDepth || m_Components[childIndex] > 4096 - components)
			{
				return R::Failure(MakeArtifactError("Task payload structure exceeds its processing bounds."));
			}

			components += m_Components[childIndex];
			depth = Max(depth, m_Depths[childIndex] + 1);
			return R::Success();
		};
		if (type.Kind == EShaderPayloadTypeKind::Scalar)
		{
			if ((type.Scalar != EShaderScalar::Float && type.Scalar != EShaderScalar::SignedInteger && type.Scalar != EShaderScalar::UnsignedInteger) || type.ScalarWidth != 32 ||
				type.Count != 0 ||
				type.ElementType != NO_PAYLOAD_TYPE ||
				!type.MemberTypes.IsEmpty())
			{
				return R::Failure(MakeArtifactError("Task payload scalar requires an exact 32-bit numeric type."));
			}

			components = 1;
		}
		else if (type.Kind == EShaderPayloadTypeKind::Vector || type.Kind == EShaderPayloadTypeKind::Array)
		{
			if (type.Scalar != EShaderScalar::None || type.ScalarWidth != 0 || !type.MemberTypes.IsEmpty() || type.Count == 0 || type.Count > 4096 ||
				(type.Kind == EShaderPayloadTypeKind::Vector && (type.Count < 2 || type.Count > 4)))
			{
				return R::Failure(MakeArtifactError("Invalid task payload vector or array shape."));
			}

			Result<void> validated = child(type.ElementType);
			if (!validated)
			{
				return validated;
			}

			if ((type.Kind == EShaderPayloadTypeKind::Vector && m_Types[type.ElementType].Kind != EShaderPayloadTypeKind::Scalar) || components > 4096 / type.Count)
			{
				return R::Failure(MakeArtifactError("Task payload array exceeds its scalar component bound."));
			}

			components *= type.Count;
		}
		else if (type.Kind == EShaderPayloadTypeKind::Structure)
		{
			if (type.Scalar != EShaderScalar::None || type.ScalarWidth != 0 || type.Count != 0 || type.ElementType != NO_PAYLOAD_TYPE || type.MemberTypes.IsEmpty() ||
				type.MemberTypes.Size() > 32)
			{
				return R::Failure(MakeArtifactError("Invalid task payload structure shape."));
			}

			for (uint32 member : type.MemberTypes)
			{
				Result<void> validated = child(member);
				if (!validated)
				{
					return validated;
				}
			}
		}
		else
		{
			return R::Failure(MakeArtifactError("Unknown task payload type kind."));
		}

		m_Components[index] = components;
		m_Depths[index] = depth;
		m_States[index] = 2;

		return R::Success();
	}

	Result<void> ShaderPayloadCanonicalizer::Validate(const ShaderLimits& limits)
	{
		using R = Result<void>;
		ASSERT(m_TaskSignature != nullptr, "Task proof validation requires a task signature.");
		if (m_TaskSignature->Proof > EShaderTaskPayloadProof::NativeSize)
		{
			return R::Failure(MakeArtifactError("Unknown task payload proof."));
		}

		if (m_TaskSignature->Proof == EShaderTaskPayloadProof::NativeSize)
		{
			if (m_RootType != NO_PAYLOAD_TYPE ||  !m_Types.IsEmpty() || !m_TaskSignature->bHasProvenByteSize || m_TaskSignature->ProvenByteSize == 0 ||
				m_TaskSignature->ProvenByteSize > 16384)
			{
				return R::Failure(MakeArtifactError("Native task payload proof requires only a bounded exact byte extent."));
			}

			return R::Success();
		}

		if (m_RootType == NO_PAYLOAD_TYPE)
		{
			if (m_TaskSignature->Proof != EShaderTaskPayloadProof::None ||  !m_Types.IsEmpty() || m_TaskSignature->bHasProvenByteSize || m_TaskSignature->ProvenByteSize != 0)
			{
				return R::Failure(MakeArtifactError("Absent task payload has nondefault metadata."));
			}

			return R::Success();
		}

		const bool bInvalidSizeProof = m_TaskSignature->bHasProvenByteSize
			? m_TaskSignature->ProvenByteSize == 0 || m_TaskSignature->ProvenByteSize > 16384 : m_TaskSignature->ProvenByteSize != 0;
		if (m_Types.Size() > 128 ||  m_Types.Size() > limits.MaximumPayloadTypes || m_RootType >= m_Types.Size() || limits.MaximumPayloadNestingDepth == 0 ||
			limits.MaximumPayloadNestingDepth > 16 ||
			m_Types[m_RootType].Kind != EShaderPayloadTypeKind::Structure ||
			bInvalidSizeProof)
		{
			return R::Failure(MakeArtifactError("Task payload root, size proof or limits are invalid."));
		}

		Result<void> structure = ValidateStructure(limits);
		if (!structure)
		{
			return structure;
		}

		if (m_TaskSignature->bHasProvenByteSize && m_TaskSignature->ProvenByteSize < m_Components[m_RootType] * 4)
		{
			return R::Failure(MakeArtifactError("Task payload native size is smaller than its scalar shape."));
		}

		return R::Success();
	}

	Result<void> ShaderPayloadCanonicalizer::ValidateStructure(const ShaderLimits& limits)
	{
		using R = Result<void>;
		if (m_Types.IsEmpty() || m_Types.Size() > 128 || m_Types.Size() > limits.MaximumPayloadTypes || m_RootType >= m_Types.Size() || limits.MaximumPayloadNestingDepth == 0 ||
			limits.MaximumPayloadNestingDepth > 16 ||
			m_Types[m_RootType].Kind != EShaderPayloadTypeKind::Structure)
		{
			return R::Failure(MakeArtifactError("Shader data structural root or processing bounds are invalid."));
		}

		Result<void> validated = validateTypeRecursive(m_RootType, limits.MaximumPayloadNestingDepth);
		if (!validated)
		{
			return validated;
		}

		uint64 members = 0;
		for (uint32 index = 0; index < m_Types.Size(); ++index)
		{
			if (m_States[index] != 2)
			{
				return R::Failure(MakeArtifactError("Task payload contains an unreachable type."));
			}

			members += m_Types[index].MemberTypes.Size();
		}

		if (members > limits.MaximumPayloadMembers)
		{
			return R::Failure(MakeArtifactError("Task payload exceeds its member bound."));
		}

		return R::Success();
	}

	uint32 ShaderPayloadCanonicalizer::GetMinimumScalarBytes() const noexcept
	{
		ASSERT(m_RootType < 128 && m_States[m_RootType] == 2, "Scalar lower bounds follow structural validation.");
		return m_Components[m_RootType] * 4;
	}

	bool ShaderPayloadCanonicalizer::typesEqual(const ShaderPayloadType& left, const ShaderPayloadType& right) noexcept
	{
		if (left.Kind != right.Kind || left.Scalar != right.Scalar || left.ScalarWidth != right.ScalarWidth || left.Count != right.Count || left.ElementType != right.ElementType ||
			left.MemberTypes.Size() != right.MemberTypes.Size())
		{
			return false;
		}

		return core::Equal(left.MemberTypes.begin(), left.MemberTypes.end(), right.MemberTypes.begin());
	}

	Result<uint32> ShaderPayloadCanonicalizer::canonicalTypeRecursive(uint32 index)
	{
		ASSERT(index < m_Types.Size() && index < 128, "Canonical traversal uses validated payload nodes.");

		using R = Result<uint32>;
		if (m_Map[index] != NO_PAYLOAD_TYPE)
		{
			return R::Success(m_Map[index]);
		}

		const ShaderPayloadType& source = m_Types[index];
		ShaderPayloadType type(m_CanonicalTypes.GetResource());
		type.Kind = source.Kind;
		type.Scalar = source.Scalar;
		type.ScalarWidth = source.ScalarWidth;
		type.Count = source.Count;
		if (source.ElementType != NO_PAYLOAD_TYPE)
		{
			Result<uint32> child = canonicalTypeRecursive(source.ElementType);
			if (!child)
			{
				return child;
			}

			type.ElementType = child.Value();
		}

		if (!type.MemberTypes.Reserve(source.MemberTypes.Size()))
		{
			return R::Failure(AllocationError());
		}

		for (uint32 member : source.MemberTypes)
		{
			Result<uint32> child = canonicalTypeRecursive(member);
			if (!child)
			{
				return child;
			}

			type.MemberTypes.AddRequired(child.Value());
		}

		for (uint32 prior = 0; prior < m_CanonicalTypes.Size(); ++prior)
		{
			if (typesEqual(type, m_CanonicalTypes[prior]))
			{
				m_Map[index] = prior;
				return R::Success(prior);
			}
		}

		const uint32 result = static_cast<uint32>(m_CanonicalTypes.Size());
		if (!m_CanonicalTypes.Add(Move(type)))
		{
			return R::Failure(AllocationError());
		}

		m_Map[index] = result;
		return R::Success(result);
	}

	Result<ShaderTaskPayloadSignature> ShaderPayloadCanonicalizer::Canonicalize()
	{
		using R = Result<ShaderTaskPayloadSignature>;
		ASSERT(m_TaskSignature != nullptr, "Task canonicalization requires a task signature.");
		ShaderTaskPayloadSignature result(m_CanonicalTypes.GetResource());
		result.Proof = m_RootType != NO_PAYLOAD_TYPE ? EShaderTaskPayloadProof::Structural : m_TaskSignature->Proof;
		result.bHasProvenByteSize = m_TaskSignature->bHasProvenByteSize;
		result.ProvenByteSize = m_TaskSignature->ProvenByteSize;
		if (m_RootType == NO_PAYLOAD_TYPE)
		{
			return R::Success(Move(result));
		}

		Result<void> canonical = CanonicalizeStructure(result.Types, result.RootType);
		if (!canonical)
		{
			return R::Failure(Move(canonical.GetError()));
		}

		return R::Success(Move(result));
	}

	Result<void> ShaderPayloadCanonicalizer::CanonicalizeStructure(Array<ShaderPayloadType>& output, uint32& rootIndex)
	{
		using R = Result<void>;
		ASSERT(m_Types.Size() <= 128 && m_RootType < m_Types.Size(), "Structural canonicalization follows validation.");
		if (!m_CanonicalTypes.Reserve(m_Types.Size()))
		{
			return R::Failure(AllocationError());
		}

		Result<uint32> root = canonicalTypeRecursive(m_RootType);
		if (!root)
		{
			return R::Failure(Move(root.GetError()));
		}

		output = Move(m_CanonicalTypes);
		rootIndex = root.Value();
		return R::Success();
	}

	bool ShaderPayloadCanonicalizer::IsCanonical(const ShaderTaskPayloadSignature& signature) noexcept
	{
		return IsCanonicalStructure(signature.Types.View(), signature.RootType);
	}

	bool ShaderPayloadCanonicalizer::IsCanonicalStructure(Span<const ShaderPayloadType> types, uint32 root) noexcept
	{
		if (root == NO_PAYLOAD_TYPE)
		{
			return types.IsEmpty();
		}

		if (types.Size() > 128 || root >= types.Size())
		{
			return false;
		}

		uint8 visited[128]{};
		uint32 next = 0;
		auto visit = [&](auto& self, uint32 index, uint32 depth) -> bool
		{
			if (index >= types.Size() || depth > 16)
			{
				return false;
			}

			if (visited[index] != 0)
			{
				return visited[index] == 2;
			}

			visited[index] = 1;
			const ShaderPayloadType& type = types[index];
			if (type.ElementType != NO_PAYLOAD_TYPE && !self(self, type.ElementType, depth + 1))
			{
				return false;
			}

			for (uint32 member : type.MemberTypes)
			{
				if (!self(self, member, depth + 1))
				{
					return false;
				}
			}

			if (index != next++)
			{
				return false;
			}

			visited[index] = 2;
			return true;
		};
		if (!visit(visit, root, 1) || next != types.Size())
		{
			return false;
		}

		for (uint32 index = 0; index < types.Size(); ++index)
		{
			for (uint32 prior = 0; prior < index; ++prior)
			{
				if (typesEqual(types[index], types[prior]))
				{
					return false;
				}
			}
		}

		return true;
	}

	bool ShaderPayloadCanonicalizer::Equal(const ShaderTaskPayloadSignature& left, const ShaderTaskPayloadSignature& right) noexcept
	{
		const EShaderTaskPayloadProof leftProof = left.RootType != NO_PAYLOAD_TYPE ? EShaderTaskPayloadProof::Structural : left.Proof;
		const EShaderTaskPayloadProof rightProof = right.RootType != NO_PAYLOAD_TYPE ? EShaderTaskPayloadProof::Structural : right.Proof;
		if (leftProof != rightProof || left.RootType != right.RootType || left.Types.Size() != right.Types.Size() ||
			(left.bHasProvenByteSize && right.bHasProvenByteSize && left.ProvenByteSize != right.ProvenByteSize))
		{
			return false;
		}

		for (uint32 index = 0; index < left.Types.Size(); ++index)
		{
			if (!typesEqual(left.Types[index], right.Types[index]))
			{
				return false;
			}
		}

		return true;
	}
} // namespace rox::shader
