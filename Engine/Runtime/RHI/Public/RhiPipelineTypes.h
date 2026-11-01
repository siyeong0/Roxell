#pragma once
#include <RhiBindingTypes.h>
#include <RhiFormat.h>
#include <RhiResourceTypes.h>
#include <RhiTypes.h>
#include <Span.hpp>
#include <Types.h>
#include <Utility.hpp>
namespace rox::rhi
{
	enum class EShadingRate : core::uint8
	{
		Rate1x1,
		Rate1x2,
		Rate2x1,
		Rate2x2,
		Rate2x4,
		Rate4x2,
		Rate4x4
	};

	enum class EShadingRateCombiner : core::uint8
	{
		Passthrough,
		Override,
		Min,
		Max,
		ApplyRelative
	};

	struct ShaderSpecialization final
	{
		core::uint32 ConstantID = 0;
		core::uint32 Value = 0;

		[[nodiscard]] static constexpr ShaderSpecialization UInt32(core::uint32 id, core::uint32 value) noexcept { return { id, value }; }

		[[nodiscard]] static constexpr ShaderSpecialization Int32(core::uint32 id, core::int32 value) noexcept
		{
			return { id, static_cast<core::uint32>(value) };
		}

		[[nodiscard]] static constexpr ShaderSpecialization Float32(core::uint32 id, core::float32 value) noexcept
		{
			return { id, core::BitCast<core::uint32>(value) };
		}
	};

	enum class EShaderStage : core::uint8
	{
		Compute,
		Vertex,
		Pixel,
		Geometry,
		Hull,
		Domain,
		Mesh,
		Task,
		RayGeneration,
		Miss,
		ClosestHit,
		AnyHit,
		Intersection,
		Callable
	};

	struct ShaderDesc final
	{
		EShaderStage Stage = EShaderStage::Compute;
		core::Span<const core::Byte> Bytecode;
		const char* EntryPoint = "main";
	};

	struct ShaderLibraryEntryDesc final
	{
		EShaderStage Stage = EShaderStage::Compute;
		core::Span<const core::Byte> Bytecode;
		const char* EntryPoint = "main";
	};

	class ShaderInput final
	{
	public:
		enum class EKind : core::uint8 { None, Code, Shader };

		ShaderInput() noexcept = default;
		[[nodiscard]] static ShaderInput Code(core::Span<const core::Byte> code, const char* entryPoint = "main") noexcept;
		[[nodiscard]] static ShaderInput Shader(ShaderHandle shader) noexcept;
		[[nodiscard]] EKind GetKind() const noexcept { return m_Kind; }
		[[nodiscard]] core::Span<const core::Byte> GetCode() const noexcept { return m_Code; }
		[[nodiscard]] const char* GetEntryPoint() const noexcept { return m_EntryPoint; }
		[[nodiscard]] ShaderHandle GetShader() const noexcept { return m_Shader; }

	private:
		EKind m_Kind = EKind::None;
		core::Span<const core::Byte> m_Code;
		const char* m_EntryPoint = nullptr;

		ShaderHandle m_Shader;
	};

	struct ShaderStageDesc final
	{
		ShaderInput Source;
		core::Span<const ShaderSpecialization> Specializations;
	};

	struct ComputePipelineDesc final
	{
		ShaderStageDesc Compute;

		core::uint32 PushConstantBytes = 0;
		core::Span<const BindingLayoutHandle> Layouts;

		bool bHeapDirectlyIndexed = false;
		bool bDeviceHeapIndexed = false;
	};

	struct VertexAttributeDesc final
	{
		core::uint32 Location = 0;
		core::uint32 OffsetBytes = 0;
		EFormat Format = EFormat::UNKNOWN;
		core::uint32 BufferSlot = 0;

		char SemanticName[32]{};
		core::uint32 SemanticIndex = 0;
	};

	struct VertexBufferSlotDesc final
	{
		core::uint32 StrideBytes = 0;
		bool bPerInstance = false;
	};

	struct InputLayoutDesc final
	{
		core::uint32 VertexStrideBytes = 0;
		core::Span<const VertexAttributeDesc> Attributes;
		core::Span<const VertexBufferSlotDesc> ExtraVertexBufferSlots;
	};

	struct VertexBufferBinding final
	{
		BufferHandle Buffer;
		core::uint64 OffsetBytes = 0;
	};

	enum class EPrimitiveTopology : core::uint8
	{
		TriangleList,
		TriangleStrip,
		LineList,
		LineStrip,
		PointList,
		PatchList
	};

	enum class ECullMode : core::uint8
	{
		None,
		Front,
		Back
	};

	enum class EFrontFace : core::uint8
	{
		CounterClockwise,
		Clockwise
	};

	enum class EFillMode : core::uint8
	{
		Solid,
		Wireframe
	};

	enum class EStencilOp : core::uint8
	{
		Keep,
		Zero,
		Replace,
		IncrementClamp,
		DecrementClamp,
		Invert,
		IncrementWrap,
		DecrementWrap
	};

	struct StencilFaceDesc final
	{
		EStencilOp FailOp = EStencilOp::Keep;
		EStencilOp DepthFailOp = EStencilOp::Keep;
		EStencilOp PassOp = EStencilOp::Keep;
		ECompareOp Compare = ECompareOp::Always;
	};

	struct StencilStateDesc final
	{
		bool bEnable = false;

		core::uint8 ReadMask = 0xff;
		core::uint8 WriteMask = 0xff;

		StencilFaceDesc FrontFace;
		StencilFaceDesc BackFace;
	};

	enum class EBlendFactor : core::uint8
	{
		Zero,
		One,
		SourceColor,
		OneMinusSourceColor,
		DestinationColor,
		OneMinusDestinationColor,
		SourceAlpha,
		OneMinusSourceAlpha,
		DestinationAlpha,
		OneMinusDestinationAlpha,
		ConstantColor,
		OneMinusConstantColor,
		SourceAlphaSaturate,
		Source1Color,
		OneMinusSource1Color,
		Source1Alpha,
		OneMinusSource1Alpha
	};

	enum class EBlendOp : core::uint8
	{
		Add,
		Subtract,
		ReverseSubtract,
		Min,
		Max
	};

	struct RasterStateDesc final
	{
		ECullMode CullMode = ECullMode::None;
		EFrontFace FrontFace = EFrontFace::CounterClockwise;
		EFillMode FillMode = EFillMode::Solid;

		bool bDepthClipEnabled = true;

		core::int32 DepthBias = 0;
		core::float32 DepthBiasClamp = 0.0f;
		core::float32 SlopeScaledDepthBias = 0.0f;

		bool bConservativeRasterEnabled = false;
	};

	struct ColorBlendDesc final
	{
		bool bEnable = false;

		EBlendFactor SourceColor = EBlendFactor::One;
		EBlendFactor DestinationColor = EBlendFactor::Zero;
		EBlendOp ColorOperation = EBlendOp::Add;

		EBlendFactor SourceAlpha = EBlendFactor::One;
		EBlendFactor DestinationAlpha = EBlendFactor::Zero;
		EBlendOp AlphaOperation = EBlendOp::Add;

		core::uint8 WriteMask = 0xF;
	};

	struct DepthBoundsDesc final
	{
		bool bEnable = false;
		// Enabled bounds are finite, ordered and within [0,1]; disabled values are ignored.
		core::float32 Minimum = 0.0f;
		core::float32 Maximum = 1.0f;
	};

	struct GraphicsPipelineDesc final
	{
		ShaderStageDesc Vertex;
		ShaderStageDesc Pixel;
		ShaderStageDesc Geometry;
		ShaderStageDesc Hull;
		ShaderStageDesc Domain;
		ShaderStageDesc Mesh;
		ShaderStageDesc Task;

		InputLayoutHandle InputLayout;
		core::uint32 VertexStrideBytes = 0;
		core::Span<const VertexAttributeDesc> VertexAttributes;
		core::Span<const VertexBufferSlotDesc> ExtraVertexBufferSlots;

		EPrimitiveTopology Topology = EPrimitiveTopology::TriangleList;
		core::uint32 PatchControlPoints = 0;
		RasterStateDesc Raster;
		core::uint32 ViewportCount = 1;
		bool bSupportsShadingRateImage = false;

		EFormat ColorFormat = EFormat::RGBA8_UNORM;
		core::Span<const EFormat> ColorFormats;
		core::Span<const ColorBlendDesc> ColorBlends;
		core::uint32 SampleCount = 1;

		EFormat DepthFormat = EFormat::UNKNOWN;
		bool bDepthTest = false;
		bool bDepthWrite = false;
		ECompareOp DepthCompare = ECompareOp::Less;
		DepthBoundsDesc DepthBounds;
		StencilStateDesc Stencil;

		core::Span<const BindingLayoutHandle> Layouts;
		core::uint32 PushConstantBytes = 0;
		bool bHeapDirectlyIndexed = false;
		bool bDeviceHeapIndexed = false;
	};
}
