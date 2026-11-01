#include "DxilReflection.h"

#include "DxcPointer.hpp"

#include <Algorithms.hpp>
#include <ByteReader.h>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		Error unsupported()
		{
			return MakeBuildError(EErrorCode::Unsupported, "Final DXIL descriptor or heap metadata does not match the advanced profile.");
		}
	} // namespace
#if defined(_WIN32)
	bool MatchesDxilBinding(const shader::ShaderBinding& binding, const D3D12_SHADER_INPUT_BIND_DESC& native)
	{
		using Kind = shader::EShaderBindingKind;

		switch (binding.Kind)
		{
		case Kind::ConstantBuffer:
			return native.Type == D3D_SIT_CBUFFER;
		case Kind::ReadOnlyBuffer:
			return native.Dimension == D3D_SRV_DIMENSION_BUFFER && native.Type == (binding.DataLayout == shader::NO_DATA_LAYOUT ? D3D_SIT_BYTEADDRESS : D3D_SIT_STRUCTURED);
		case Kind::ReadWriteBuffer:
			return native.Dimension == D3D_SRV_DIMENSION_BUFFER &&
				native.Type == (binding.DataLayout == shader::NO_DATA_LAYOUT ? D3D_SIT_UAV_RWBYTEADDRESS : D3D_SIT_UAV_RWSTRUCTURED);
		case Kind::Sampler:
			return native.Type == D3D_SIT_SAMPLER;
		case Kind::AccelerationStructure:
			return native.Type == D3D_SIT_RTACCELERATIONSTRUCTURE;
		case Kind::SampledTexture:
		case Kind::StorageTexture:
			if (native.Type != (binding.Kind == Kind::SampledTexture ? D3D_SIT_TEXTURE : D3D_SIT_UAV_RWTYPED))
			{
				return false;
			}

			break;

		default:
		{
			break;
		}
		}

		using Shape = shader::EShaderTextureShape;

		switch (binding.TextureShape)
		{
		case Shape::Texture1D:
			return native.Dimension == (binding.bTextureArray ? D3D_SRV_DIMENSION_TEXTURE1DARRAY : D3D_SRV_DIMENSION_TEXTURE1D);
		case Shape::Texture2D:
			return native.Dimension ==
				(binding.bMultisampled ? (binding.bTextureArray ? D3D_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D_SRV_DIMENSION_TEXTURE2DMS)
									: (binding.bTextureArray ? D3D_SRV_DIMENSION_TEXTURE2DARRAY : D3D_SRV_DIMENSION_TEXTURE2D));
		case Shape::Texture3D:
			return native.Dimension == D3D_SRV_DIMENSION_TEXTURE3D;
		case Shape::Cube:
			return native.Dimension == (binding.bTextureArray ? D3D_SRV_DIMENSION_TEXTURECUBEARRAY : D3D_SRV_DIMENSION_TEXTURECUBE);
		case Shape::Buffer:
			return native.Dimension == D3D_SRV_DIMENSION_BUFFER;
		case Shape::None:
			return false;

		default:
		{
			break;
		}
		}

		return false;
	}
#endif
	Result<void> ReflectDxilBindings(shader::ShaderVariant& variant, const shader::ShaderEntryPoint& entry, CancellationToken cancellation)
	{
		using R = Result<void>;

		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}
#if defined(_WIN32)
		DxcPointer<IDxcUtils> utils;
		if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(utils.Put()))))
		{
			return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Final DXIL reflection initialization failed."));
		}

		const DxcBuffer buffer{variant.Bytecode.Data(), variant.Bytecode.Size(), 0};
		DxcPointer<ID3D12ShaderReflection> shaderReflection;
		DxcPointer<ID3D12LibraryReflection> libraryReflection;
		ID3D12FunctionReflection* functionReflection = nullptr;
		uint64 flags = 0;
		uint32 resourceCount = 0;
		if (entry.Stage >= shader::EShaderStage::RayGeneration)
		{
			if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(libraryReflection.Put()))))
			{
				return R::Failure(unsupported());
			}

			D3D12_LIBRARY_DESC library{};
			if (FAILED(libraryReflection->GetDesc(&library)) || library.FunctionCount > 128)
			{
				return R::Failure(unsupported());
			}

			for (UINT index = 0; index < library.FunctionCount; ++index)
			{
				if (cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				ID3D12FunctionReflection* function = libraryReflection->GetFunctionByIndex(static_cast<INT>(index));
				D3D12_FUNCTION_DESC desc{};
				if (function == nullptr || FAILED(function->GetDesc(&desc)) || desc.Name == nullptr)
				{
					return R::Failure(unsupported());
				}

				if (StringView(desc.Name) != variant.EmittedEntryPoint.View() && library.FunctionCount != 1)
				{
					continue;
				}

				if (functionReflection != nullptr)
				{
					return R::Failure(unsupported());
				}

				functionReflection = function;
				flags = desc.RequiredFeatureFlags;
				resourceCount = desc.BoundResources;
			}

			if (functionReflection == nullptr)
			{
				return R::Failure(unsupported());
			}
		}
		else
		{
			if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(shaderReflection.Put()))))
			{
				return R::Failure(unsupported());
			}

			flags = shaderReflection->GetRequiresFlags();
			D3D12_SHADER_DESC desc{};
			if (FAILED(shaderReflection->GetDesc(&desc)))
			{
				return R::Failure(unsupported());
			}

			resourceCount = desc.BoundResources;
		}

		if (resourceCount > 4096)
		{
			return R::Failure(unsupported());
		}

		if ((flags & D3D_SHADER_REQUIRES_MINIMUM_PRECISION) != 0)
		{
			variant.Requirements.Features |= static_cast<uint64>(shader::EShaderFeature::MinimumPrecision);
		}

		if ((flags & D3D_SHADER_REQUIRES_TYPED_UAV_LOAD_ADDITIONAL_FORMATS) != 0)
		{
			variant.Requirements.Features |= static_cast<uint64>(shader::EShaderFeature::TypedUavLoadAdditionalFormats);
		}

		if ((flags & D3D_SHADER_REQUIRES_WAVE_OPS) != 0)
		{
			variant.Requirements.Features |= static_cast<uint64>(shader::EShaderFeature::Subgroup);
		}

		if ((flags & D3D_SHADER_REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING) != 0)
		{
			variant.Requirements.Features |= static_cast<uint64>(shader::EShaderFeature::ResourceDescriptorHeap);
		}

		if ((flags & D3D_SHADER_REQUIRES_SAMPLER_DESCRIPTOR_HEAP_INDEXING) != 0)
		{
			variant.Requirements.Features |= static_cast<uint64>(shader::EShaderFeature::SamplerDescriptorHeap);
		}

		// Prove every final resource against the source layout before discarding unused declarations.
		for (uint32 index = 0; index < resourceCount; ++index)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			D3D12_SHADER_INPUT_BIND_DESC desc{};
			if (FAILED(functionReflection != nullptr ? functionReflection->GetResourceBindingDesc(index, &desc) : shaderReflection->GetResourceBindingDesc(index, &desc)))
			{
				return R::Failure(unsupported());
			}

			uint32 matches = 0;

			for (const ShaderBinding& binding : variant.Bindings)
			{
				if (desc.Space != binding.Space || desc.BindPoint != binding.Binding || !MatchesDxilBinding(binding, desc))
				{
					continue;
				}

				const uint32 expected = binding.CountMode == shader::EShaderDescriptorCountMode::Runtime
					? (binding.Kind == shader::EShaderBindingKind::ConstantBuffer ? 0xffffffffu : 0u)
					: binding.ArrayCount;
				if (desc.BindCount != expected || ++matches != 1)
				{
					return R::Failure(unsupported());
				}
			}

			if (matches != 1)
			{
				return R::Failure(unsupported());
			}
		}

		for (uint32 bindingIndex = static_cast<uint32>(variant.Bindings.Size()); bindingIndex > 0;)
		{
			--bindingIndex;
			const ShaderBinding& binding = variant.Bindings[bindingIndex];
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			if (binding.CountMode != shader::EShaderDescriptorCountMode::Runtime)
			{
				continue;
			}

			// Slang renames resources during final lowering; native register/type identity is authoritative.
			uint32 matches = 0;

			for (uint32 index = 0; index < resourceCount; ++index)
			{
				if (cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				D3D12_SHADER_INPUT_BIND_DESC desc{};
				const HRESULT result = functionReflection != nullptr ? functionReflection->GetResourceBindingDesc(index, &desc)
																	: shaderReflection->GetResourceBindingDesc(index, &desc);
				if (FAILED(result))
				{
					return R::Failure(unsupported());
				}

				if (desc.Space == binding.Space && desc.BindPoint == binding.Binding && MatchesDxilBinding(binding, desc))
				{
					if (desc.BindCount != (binding.Kind == shader::EShaderBindingKind::ConstantBuffer ? 0xffffffffu : 0u))
					{
						return R::Failure(unsupported());
					}

					++matches;
				}
			}

			if (matches > 1)
			{
				return R::Failure(unsupported());
			}

			if (matches == 0)
			{
				// PreserveParameters retains globals that the selected entry's final code removes.
				RemoveUnusedBinding(variant, bindingIndex);
			}
		}

		if (NoneOf(variant.Bindings.begin(), variant.Bindings.end(), [](const ShaderBinding& binding) { return binding.CountMode == shader::EShaderDescriptorCountMode::Runtime; }))
		{
			variant.Requirements.Features &= ~static_cast<uint64>(shader::EShaderFeature::RuntimeDescriptorArray);
		}

		return R::Success();
#else
		(void)variant;
		(void)entry;

		return R::Failure(unsupported());
#endif
	}
} // namespace rox::shader
