#pragma once

#include <Cancellation.h>
#include <ShaderOptions.h>

namespace rox::shader
{
	enum class EShaderBindingKind : core::uint32
	{
		ConstantBuffer,
		ReadOnlyBuffer,
		ReadWriteBuffer,
		SampledTexture,
		StorageTexture,
		Sampler,
		AccelerationStructure
	};

	enum class EShaderRegisterClass : core::uint32
	{
		Unified,
		ConstantBuffer,
		ShaderResource,
		UnorderedAccess,
		Sampler
	};

	enum class EShaderTextureShape : core::uint32
	{
		None,
		Texture1D,
		Texture2D,
		Texture3D,
		Cube,
		Buffer
	};

	enum class EShaderResourceAccess : core::uint32
	{
		Read = 1,
		Write = 2,
		ReadWrite = 3,
		ReadWriteAtomic = 7
	};

	struct ShaderDataField
	{
		core::String Name;

		core::uint64 ByteOffset = 0;
		core::uint64 ByteSize = 0;

		EShaderScalar Scalar = EShaderScalar::None;
		core::uint32 ScalarWidth = 0;
		core::uint32 VectorWidth = 1;

		core::uint32 MatrixRows = 1;
		core::uint32 MatrixColumns = 1;
		EShaderMatrixOrder MatrixOrder = EShaderMatrixOrder::None;
		core::uint64 MatrixStride = 0;

		core::uint32 ArrayCount = 1;
		core::uint64 ArrayStride = 0;

		core::uint32 NestedLayout = NO_DATA_LAYOUT;

		explicit ShaderDataField(core::MemoryResource& resource) noexcept
			: Name(resource)
		{
		}

		ShaderDataField(ShaderDataField&&) noexcept = default;
		ShaderDataField& operator=(ShaderDataField&&) noexcept = default;

		ShaderDataField(const ShaderDataField&) = delete;
		ShaderDataField& operator=(const ShaderDataField&) = delete;
	};

	struct ShaderDataLayout
	{
		core::uint64 ByteSize = 0;
		core::uint32 Alignment = 1;
		core::uint64 ElementStride = 0;

		core::Array<ShaderDataField> Fields;

		core::Hash256 Identity;

		explicit ShaderDataLayout(core::MemoryResource& resource) noexcept
			: Fields(resource)
		{
		}

		ShaderDataLayout(ShaderDataLayout&&) noexcept = default;
		ShaderDataLayout& operator=(ShaderDataLayout&&) noexcept = default;

		ShaderDataLayout(const ShaderDataLayout&) = delete;
		ShaderDataLayout& operator=(const ShaderDataLayout&) = delete;
	};

	enum class EShaderDescriptorCountMode : core::uint32
	{
		Fixed,
		Runtime
	};

	enum class EShaderBindingScope : core::uint32
	{
		Global,
		LocalRecord
	};

	struct ShaderBinding
	{
		core::String Name;
		EShaderBindingKind Kind = EShaderBindingKind::ConstantBuffer;

		core::uint32 Space = 0;
		core::uint32 Binding = 0;
		EShaderRegisterClass RegisterNamespace = EShaderRegisterClass::ConstantBuffer;

		core::uint32 ArrayCount = 1;
		EShaderDescriptorCountMode CountMode = EShaderDescriptorCountMode::Fixed;
		EShaderBindingScope Scope = EShaderBindingScope::Global;

		core::uint32 DataLayout = NO_DATA_LAYOUT;

		EShaderTextureShape TextureShape = EShaderTextureShape::None;
		bool bTextureArray = false;
		bool bMultisampled = false;

		EShaderResourceAccess Access = EShaderResourceAccess::Read;

		explicit ShaderBinding(core::MemoryResource& resource) noexcept
			: Name(resource)
		{
		}

		ShaderBinding(ShaderBinding&&) noexcept = default;
		ShaderBinding& operator=(ShaderBinding&&) noexcept = default;

		ShaderBinding(const ShaderBinding&) = delete;
		ShaderBinding& operator=(const ShaderBinding&) = delete;
	};

	struct ShaderResourceUse
	{
		core::uint32 BindingIndex = 0;
		EShaderResourceAccess Access = EShaderResourceAccess::Read;
	};
	enum class EShaderBuiltin : core::uint32
	{
		None,
		Position,
		VertexIndex,
		InstanceIndex,
		FrontFace,
		FragDepth,
		SampleIndex,
		PrimitiveIndex,
		SampleMask,
		RenderTargetArrayIndex,
		ViewportIndex,
		BaseVertex,
		BaseInstance,
		DrawIndex
	};

	enum class EShaderInterpolation : core::uint32
	{
		Unspecified,
		Flat,
		Linear,
		NoPerspective
	};

	enum class EShaderStageIORate : core::uint32
	{
		Unspecified,
		PerVertex,
		PerPrimitive
	};

	enum class EShaderMeshTopology : core::uint32
	{
		None,
		Points,
		Lines,
		Triangles
	};

	enum class EShaderPayloadTypeKind : core::uint32
	{
		Scalar,
		Vector,
		Array,
		Structure
	};

	enum class EShaderPushConstantLowering : core::uint32
	{
		VulkanPushConstant,
		D3D12RootConstants
	};

	enum class EShaderStage : core::uint32
	{
		Vertex,
		Pixel,
		Compute,
		Geometry,
		Hull,
		Domain,
		Task,
		Mesh,
		RayGeneration,
		Intersection,
		AnyHit,
		ClosestHit,
		Miss,
		Callable
	};

	struct ShaderEntryPoint
	{
		core::String Name;
		EShaderStage Stage = EShaderStage::Compute;

		core::uint32 ThreadGroupSize[3] = {};

		explicit ShaderEntryPoint(core::MemoryResource& resource) noexcept
			: Name(resource)
		{
		}

		ShaderEntryPoint(ShaderEntryPoint&&) noexcept = default;
		ShaderEntryPoint& operator=(ShaderEntryPoint&&) noexcept = default;

		ShaderEntryPoint(const ShaderEntryPoint&) = delete;
		ShaderEntryPoint& operator=(const ShaderEntryPoint&) = delete;
	};

	struct ShaderStageIO
	{
		core::String Name;
		core::String Semantic;
		core::uint32 SemanticIndex = 0;

		// SPIR-V location or allocated DXIL signature row. NO_STAGE_LOCATION denotes an unallocated builtin
		// or incomplete DXIL packing metadata; Component is the starting native lane.
		core::uint32 Location = NO_STAGE_LOCATION;
		core::uint32 Component = 0;
		EShaderBuiltin Builtin = EShaderBuiltin::None;

		EShaderScalar Scalar = EShaderScalar::Float;
		core::uint32 ScalarWidth = 32;
		core::uint32 VectorWidth = 1;
		core::uint32 ArrayCount = 1;

		EShaderInterpolation Interpolation = EShaderInterpolation::Unspecified;
		bool bCentroid = false;
		bool bSample = false;
		EShaderStageIORate Rate = EShaderStageIORate::Unspecified;

		explicit ShaderStageIO(core::MemoryResource& resource) noexcept
			: Name(resource)
			, Semantic(resource)
		{
		}

		ShaderStageIO(ShaderStageIO&&) noexcept = default;
		ShaderStageIO& operator=(ShaderStageIO&&) noexcept = default;

		ShaderStageIO(const ShaderStageIO&) = delete;
		ShaderStageIO& operator=(const ShaderStageIO&) = delete;
	};

	struct ShaderMeshOutput
	{
		EShaderMeshTopology Topology = EShaderMeshTopology::None;
		core::uint32 MaximumVertices = 0;
		core::uint32 MaximumPrimitives = 0;

		auto operator<=>(const ShaderMeshOutput&) const = default;
	};

	struct ShaderPayloadType
	{
		EShaderPayloadTypeKind Kind = EShaderPayloadTypeKind::Scalar;

		EShaderScalar Scalar = EShaderScalar::None;
		core::uint32 ScalarWidth = 0;

		core::uint32 Count = 0;
		core::uint32 ElementType = NO_PAYLOAD_TYPE;

		core::Array<core::uint32> MemberTypes;

		explicit ShaderPayloadType(core::MemoryResource& resource) noexcept
			: MemberTypes(resource)
		{
		}
	};

	enum class EShaderTaskPayloadProof : core::uint32
	{
		None,
		Structural,
		NativeSize
	};

	struct ShaderTaskPayloadSignature
	{
		EShaderTaskPayloadProof Proof = EShaderTaskPayloadProof::None;

		core::Array<ShaderPayloadType> Types;
		core::uint32 RootType = NO_PAYLOAD_TYPE;

		bool bHasProvenByteSize = false;
		core::uint32 ProvenByteSize = 0;

		explicit ShaderTaskPayloadSignature(core::MemoryResource& resource) noexcept
			: Types(resource)
		{
		}
	};

	struct ShaderRayDeclaredLimits
	{
		core::uint32 MaximumPayloadBytes = 0;
		core::uint32 MaximumAttributeBytes = 0;
		core::uint32 MaximumCallableBytes = 0;

		auto operator<=>(const ShaderRayDeclaredLimits&) const = default;
	};

	enum class EShaderRayDataKind : core::uint32
	{
		Payload,
		HitAttributes,
		Callable
	};

	enum class EShaderRayDataDirection : core::uint32
	{
		Incoming,
		Outgoing
	};

	struct ShaderRayDataSignature
	{
		EShaderRayDataKind Kind = EShaderRayDataKind::Payload;
		EShaderRayDataDirection Direction = EShaderRayDataDirection::Incoming;
		core::uint32 Location = NO_STAGE_LOCATION;

		core::Array<ShaderPayloadType> Types;
		core::uint32 RootType = NO_PAYLOAD_TYPE;

		bool bHasProvenByteSize = false;
		core::uint32 ProvenByteSize = 0;

		explicit ShaderRayDataSignature(core::MemoryResource& resource) noexcept
			: Types(resource)
		{
		}
	};

	struct ShaderRayInterface
	{
		ShaderRayDeclaredLimits DeclaredLimits;

		core::Array<ShaderRayDataSignature> Data;
		bool bOutgoingDataComplete = false;

		explicit ShaderRayInterface(core::MemoryResource& resource) noexcept
			: Data(resource)
		{
		}
	};

	struct ShaderPushConstantRange
	{
		core::uint32 ByteOffset = 0;
		core::uint32 ByteSize = 0;
		core::uint32 DataLayout = NO_DATA_LAYOUT;

		core::uint64 StageMask = 0;

		EShaderPushConstantLowering Lowering = EShaderPushConstantLowering::VulkanPushConstant;
		core::uint32 Space = 0;
		core::uint32 Binding = 0;
	};

	enum class EShaderLocalRecordLowering : core::uint32
	{
		None,
		VulkanShaderRecord,
		D3D12LocalRootArguments
	};

	enum class EShaderLocalRecordProof : core::uint32
	{
		None,
		FinalSpirVStorage,
		DeclaredDxilRecipe
	};

	enum class EShaderLocalRootArgumentKind : core::uint32
	{
		Constants32,
		ConstantBufferAddress,
		ShaderResourceAddress,
		UnorderedAccessAddress,
		DescriptorTable
	};

	struct ShaderLocalRootArgument
	{
		EShaderLocalRootArgumentKind Kind = EShaderLocalRootArgumentKind::Constants32;
		core::uint32 BindingIndex = 0;

		core::uint32 ByteOffset = 0;
		core::uint32 ValueCount = 0;

		auto operator<=>(const ShaderLocalRootArgument&) const = default;
	};

	struct ShaderLocalRecord
	{
		EShaderLocalRecordLowering Lowering = EShaderLocalRecordLowering::None;

		core::uint32 DataLayout = NO_DATA_LAYOUT;
		core::uint32 ByteSize = 0;
		core::uint32 Alignment = 0;

		EShaderLocalRecordProof Proof = EShaderLocalRecordProof::None;

		auto operator<=>(const ShaderLocalRecord&) const = default;
	};
	struct ShaderVariant
	{
		ShaderTargetProfile Target;
		core::uint32 EntryPointIndex = 0;
		core::String EmittedEntryPoint;
		core::Array<core::Byte> Bytecode;

		core::Array<ShaderDataLayout> Layouts;
		core::Array<ShaderBinding> Bindings;
		core::Array<ShaderResourceUse> ResourceUses;

		core::uint64 RequiredCapabilities = 0;
		ShaderFeatureRequirements Requirements;

		bool bStageIOComplete = false;
		core::Array<ShaderStageIO> StageInputs;
		core::Array<ShaderStageIO> StageOutputs;

		core::Array<ShaderPushConstantRange> PushConstants;
		core::Array<ShaderSpecializationParameter> SpecializationParameters;

		ShaderMeshOutput MeshOutput;
		ShaderTaskPayloadSignature TaskPayload;

		ShaderRayInterface RayInterface;
		ShaderLocalRecord LocalRecord;
		core::Array<ShaderLocalRootArgument> LocalRootArguments;

		explicit ShaderVariant(core::MemoryResource& resource) noexcept
			: EmittedEntryPoint(resource)
			, Bytecode(resource)
			, Layouts(resource)
			, Bindings(resource)
			, ResourceUses(resource)
			, StageInputs(resource)
			, StageOutputs(resource)
			, PushConstants(resource)
			, SpecializationParameters(resource)
			, TaskPayload(resource)
			, RayInterface(resource)
			, LocalRootArguments(resource)
		{
		}

		ShaderVariant(ShaderVariant&&) noexcept = default;
		ShaderVariant& operator=(ShaderVariant&&) noexcept = default;

		ShaderVariant(const ShaderVariant&) = delete;
		ShaderVariant& operator=(const ShaderVariant&) = delete;
	};

	struct ShaderLimits
	{
		core::uint64 MaximumTotalBytes = 64 * 1024 * 1024;

		core::uint32 MaximumEntryPoints = 256;
		core::uint32 MaximumVariants = 512;

		core::uint32 MaximumBindings = 4096;
		core::uint32 MaximumLayouts = 4096;
		core::uint32 MaximumFields = 65536;

		core::uint32 MaximumNameBytes = 4096;
		core::uint64 MaximumCodeBytes = 16 * 1024 * 1024;
		core::uint32 MaximumNestingDepth = 32;

		core::uint32 MaximumStageIO = 4096;
		core::uint32 MaximumPushConstantRanges = 256;
		core::uint32 MaximumSpecializationParameters = 1024;

		core::uint32 MaximumRayDataSignatures = 1024;
		core::uint32 MaximumPayloadTypes = 4096;
		core::uint32 MaximumPayloadMembers = 16384;
		core::uint32 MaximumPayloadNestingDepth = 16;
	};
	struct ShaderEntrySelection;

	struct ShaderProgramBinding
	{
		ShaderBinding Binding;
		core::Hash256 ParameterID;

		core::uint64 StageMask = 0;
		core::uint32 SelectionIndex = 0;

		explicit ShaderProgramBinding(core::MemoryResource& resource) noexcept
			: Binding(resource)
		{
		}
	};

	struct ShaderProgramPushConstant
	{
		ShaderPushConstantRange Range;
		core::uint32 SelectionIndex = 0;
	};

	enum class EShaderStageIOStatus
	{
		Unverified,
		Verified
	};

	// NativeSizeVerified proves only native extent equality, with no payload type or semantic guarantee.
	enum class EShaderTaskPayloadCompatibility
	{
		Absent,
		StructuralVerified,
		NativeSizeVerified
	};

	enum class EShaderInterfaceCompatibility
	{
		Identical,
		BindingCompatible,
		RepackRequired,
		Incompatible
	};

	enum class EShaderSemanticCompatibility
	{
		Unproven
	};

	struct ShaderInterfaceCompatibility
	{
		EShaderInterfaceCompatibility Level = EShaderInterfaceCompatibility::Incompatible;
		EShaderSemanticCompatibility Semantic = EShaderSemanticCompatibility::Unproven;

		bool bCodeChanged = false;
		bool bAccessChanged = false;
		bool bRequirementsChanged = false;

		core::Array<core::String> Reasons;

		explicit ShaderInterfaceCompatibility(core::MemoryResource& resource) noexcept
			: Reasons(resource)
		{
		}
	};

	struct ShaderPropertyLocation
	{
		core::uint32 SelectionIndex = 0;
		core::uint32 BindingIndex = 0;
		core::uint32 FieldIndex = NO_DATA_LAYOUT;
	};

	// Retains its selections. Borrowed bindings/layout references remain live while any handle is retained.
	class ShaderLayout
	{
	private:
		struct Storage;

	public:
		[[nodiscard]] static core::Result<ShaderLayout> Create(core::Span<const ShaderEntrySelection>, core::MemoryResource&, core::CancellationToken = {});

		ShaderLayout() noexcept;
		~ShaderLayout();
		ShaderLayout(const ShaderLayout&) noexcept;
		ShaderLayout(ShaderLayout&&) noexcept;
		ShaderLayout& operator=(const ShaderLayout&) noexcept;
		ShaderLayout& operator=(ShaderLayout&&) noexcept;

		bool IsValid() const noexcept;
		core::Span<const ShaderEntrySelection> GetSelections() const noexcept;
		core::Span<const ShaderProgramBinding> GetBindings() const noexcept;
		ShaderTargetProfile GetTarget() const noexcept;
		core::Hash256 GetPhysicalLayoutHash() const noexcept;
		core::Hash256 GetLayoutHash() const noexcept;
		core::Hash256 GetSelectedCodeHash() const noexcept;
		core::uint64 GetRequiredCapabilities() const noexcept;
		ShaderFeatureRequirements GetFeatureRequirements() const noexcept;
		core::Span<const ShaderProgramPushConstant> GetPushConstants() const noexcept;
		core::Span<const ShaderSpecializationParameter> GetSpecializationParameters() const noexcept;
		EShaderStageIOStatus GetStageIOStatus() const noexcept;
		EShaderTaskPayloadCompatibility GetTaskPayloadCompatibility() const noexcept;
		core::Result<core::uint32> FindParameter(const core::Hash256&) const;
		// Returns the exact per-entry locations for a public property in the selected code.
		[[nodiscard]] core::Result<core::Array<ShaderPropertyLocation>> FindPropertyBindings(core::StringView, core::MemoryResource&) const;

	private:
		core::SharedPtr<Storage> m_Storage;
	};

	[[nodiscard]] core::Result<ShaderInterfaceCompatibility> CompareShaderLayouts(const ShaderLayout&, const ShaderLayout&, core::MemoryResource&);

	// Authored mapping to one resource or one top-level numeric field of an exact compiled entry.
	// Nested/array data remains available through explicit ShaderBufferWriter host layouts.
	struct ShaderPropertyBinding
	{
		core::String Property;

		core::String Entry;
		ShaderTargetProfile Target;

		core::String Binding;
		core::String Field;

		explicit ShaderPropertyBinding(core::MemoryResource& memory)
			: Property(memory)
			, Entry(memory)
			, Binding(memory)
			, Field(memory)
		{
		}
	};

	[[nodiscard]] core::Result<core::Hash256> ComputeShaderLayoutIdentity(const ShaderDataLayout&, core::MemoryResource&);
	// Requires a variant from a validated artifact. Names and proof provenance do not affect native packing.
	// A matching hash is a cache key; collision-critical admission must also compare the full recipe.
	[[nodiscard]] core::Result<core::Hash256> ComputeShaderLocalRecordPhysicalIdentity(const ShaderVariant&, core::MemoryResource&);

} // namespace rox::shader
