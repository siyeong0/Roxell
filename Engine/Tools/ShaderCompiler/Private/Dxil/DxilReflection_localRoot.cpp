#include "DxilReflection.h"

#include "DxcPointer.hpp"

#include <Algorithms.hpp>
#include <Comparison.hpp>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	Result<void> ReflectDxilLocalRoot(
		shader::ShaderVariant& variant,
		const shader::ShaderEntryPoint& entry,
		const ShaderCompileRequest& request,
		MemoryResource& resource,
		CancellationToken cancellation)
	{
		using R = Result<void>;

		auto unsupported = []
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Final selected DXIL resources do not prove the declared local root recipe."));
		};

		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}
#if defined(_WIN32)
		DxcPointer<IDxcUtils> utils;
		if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(utils.Put()))))
		{
			return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Final DXIL local recipe reflection failed."));
		}

		const DxcBuffer bytecode{variant.Bytecode.Data(), variant.Bytecode.Size(), 0};
		DxcPointer<ID3D12ShaderReflection> shaderReflection;
		DxcPointer<ID3D12LibraryReflection> libraryReflection;
		ID3D12FunctionReflection* function = nullptr;
		uint32 count = 0;
		if (entry.Stage >= shader::EShaderStage::RayGeneration)
		{
			if (FAILED(utils->CreateReflection(&bytecode, IID_PPV_ARGS(libraryReflection.Put()))))
			{
				return unsupported();
			}

			D3D12_LIBRARY_DESC library{};
			if (FAILED(libraryReflection->GetDesc(&library)) || library.FunctionCount == 0 || library.FunctionCount > 128)
			{
				return unsupported();
			}

			for (UINT index = 0; index < library.FunctionCount; ++index)
			{
				if (cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				ID3D12FunctionReflection* candidate = libraryReflection->GetFunctionByIndex(static_cast<INT>(index));
				D3D12_FUNCTION_DESC desc{};
				if (candidate == nullptr || FAILED(candidate->GetDesc(&desc)) || desc.Name == nullptr)
				{
					return unsupported();
				}

				if (StringView(desc.Name) != variant.EmittedEntryPoint.View() && library.FunctionCount != 1)
				{
					continue;
				}

				if (function != nullptr)
				{
					return unsupported();
				}

				function = candidate;
				count = desc.BoundResources;
			}

			if (function == nullptr)
			{
				return unsupported();
			}
		}
		else
		{
			if (FAILED(utils->CreateReflection(&bytecode, IID_PPV_ARGS(shaderReflection.Put()))))
			{
				return unsupported();
			}

			D3D12_SHADER_DESC desc{};
			if (FAILED(shaderReflection->GetDesc(&desc)))
			{
				return unsupported();
			}

			count = desc.BoundResources;
		}

		if (count > 4096)
		{
			return unsupported();
		}

		Array<D3D12_SHADER_INPUT_BIND_DESC> native(resource);
		Array<uint32> sources(resource);
		Array<uint32> nativeIndices(resource);
		if (!native.Resize(count) || !sources.Reserve(count) || !nativeIndices.Reserve(count))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (uint32 index = 0; index < count; ++index)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			if (FAILED(function != nullptr ? function->GetResourceBindingDesc(index, &native[index]) : shaderReflection->GetResourceBindingDesc(index, &native[index])))
			{
				return unsupported();
			}

			uint32 found = 0xffffffffu;

			for (uint32 source = 0; source < variant.Bindings.Size(); ++source)
			{
				const ShaderBinding& binding = variant.Bindings[source];
				if (binding.Space != native[index].Space || binding.Binding != native[index].BindPoint || !MatchesDxilBinding(binding, native[index]))
				{
					continue;
				}

				const uint32 expected = binding.CountMode == shader::EShaderDescriptorCountMode::Runtime
					? (binding.Kind == shader::EShaderBindingKind::ConstantBuffer ? 0xffffffffu : 0u)
					: binding.ArrayCount;
				if (found != 0xffffffffu || native[index].BindCount != expected)
				{
					return unsupported();
				}

				found = source;
			}

			if (found == 0xffffffffu || Find(sources.begin(), sources.end(), found) != sources.end())
			{
				return unsupported();
			}

			sources.AddRequired(found);
			nativeIndices.AddRequired(index);
		}

		Array<shader::ShaderBinding> bindings(resource);
		Array<shader::ShaderResourceUse> uses(resource);
		if (!bindings.Reserve(count) || !uses.Reserve(count))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (uint32 source : sources)
		{
			shader::ShaderResourceUse use;
			use.BindingIndex = static_cast<uint32>(bindings.Size());
			use.Access = variant.Bindings[source].Access;
			bindings.AddRequired(Move(variant.Bindings[source]));
			uses.AddRequired(use);
		}

		variant.Bindings = Move(bindings);
		variant.ResourceUses = Move(uses);
		if (NoneOf(variant.Bindings.begin(), variant.Bindings.end(), [](const ShaderBinding& binding) { return binding.CountMode == shader::EShaderDescriptorCountMode::Runtime; }))
		{
			variant.Requirements.Features &= ~static_cast<uint64>(shader::EShaderFeature::RuntimeDescriptorArray);
		}

		const ShaderLocalRootRecipe* recipe = nullptr;

		for (const ShaderLocalRootRecipe& candidate : request.LocalRootRecipes)
		{
			if (candidate.EntryPointName == entry.Name)
			{
				recipe = &candidate;
			}
		}

		if (recipe == nullptr)
		{
			return R::Success();
		}

		if (!variant.LocalRootArguments.Reserve(recipe->Arguments.Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		uint32 extent = 0;
		uint32 alignment = 4;

		for (const ShaderLocalRootArgumentDeclaration& declaration : recipe->Arguments)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			uint32 bindingIndex = 0xffffffffu;

			for (uint32 index = 0; index < variant.Bindings.Size(); ++index)
			{
				if (variant.Bindings[index].Name == declaration.BindingName)
				{
					bindingIndex = index;
				}
			}

			if (bindingIndex == 0xffffffffu)
			{
				return unsupported();
			}

			ShaderBinding& binding = variant.Bindings[bindingIndex];
			if (binding.Scope != shader::EShaderBindingScope::Global || binding.CountMode != shader::EShaderDescriptorCountMode::Fixed)
			{
				return unsupported();
			}

			const bool constants = declaration.Kind == shader::EShaderLocalRootArgumentKind::Constants32;
			if (constants)
			{
				if (function == nullptr || binding.Kind != shader::EShaderBindingKind::ConstantBuffer || binding.DataLayout >= variant.Layouts.Size())
				{
					return unsupported();
				}

				const _D3D12_SHADER_INPUT_BIND_DESC& reflected = native[nativeIndices[bindingIndex]];
				ID3D12ShaderReflectionConstantBuffer* buffer = function->GetConstantBufferByName(reflected.Name);
				D3D12_SHADER_BUFFER_DESC desc{};
				if (buffer == nullptr || FAILED(buffer->GetDesc(&desc)) || desc.Variables == 0 || desc.Variables > 4096)
				{
					return unsupported();
				}

				uint64 usedBytes = 0;

				for (UINT index = 0; index < desc.Variables; ++index)
				{
					ID3D12ShaderReflectionVariable* variable = buffer->GetVariableByIndex(index);
					D3D12_SHADER_VARIABLE_DESC value{};
					if (variable == nullptr || FAILED(variable->GetDesc(&value)))
					{
						return unsupported();
					}

					usedBytes = Max(usedBytes, uint64(value.StartOffset) + value.Size);
				}

				if (usedBytes != variant.Layouts[binding.DataLayout].ByteSize || usedBytes > uint64(declaration.ValueCount) * 4)
				{
					return unsupported();
				}
			}

			const uint32 argAlignment = constants ? 4u : 8u;
			extent = (extent + argAlignment - 1) & ~(argAlignment - 1);
			const uint32 bytes = constants ? declaration.ValueCount * 4 : 8u;
			if (extent > shader::MAXIMUM_DXIL_LOCAL_ROOT_BYTES || bytes > shader::MAXIMUM_DXIL_LOCAL_ROOT_BYTES - extent)
			{
				return unsupported();
			}

			shader::ShaderLocalRootArgument argument{declaration.Kind, bindingIndex, extent, declaration.ValueCount};
			variant.LocalRootArguments.AddRequired(argument);
			binding.Scope = shader::EShaderBindingScope::LocalRecord;
			extent += bytes;
			alignment = Max(alignment, argAlignment);
		}

		variant.LocalRecord = {shader::EShaderLocalRecordLowering::D3D12LocalRootArguments, shader::NO_DATA_LAYOUT, extent, alignment,
			shader::EShaderLocalRecordProof::DeclaredDxilRecipe};

		return R::Success();
#else
		(void)variant;
		(void)entry;
		(void)request;

		return unsupported();
#endif
	}
} // namespace rox::shader
