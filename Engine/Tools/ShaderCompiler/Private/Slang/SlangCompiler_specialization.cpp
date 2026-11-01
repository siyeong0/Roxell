#include "SlangCompiler.h"

#include "../ShaderCompilerInternal.h"

#include <Assertion.h>
#include <StringView.hpp>
#include <SystemError.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	Result<void> SlangCompiler::captureSpecializationParameters(slang::IEntryPoint& entry, ShaderModuleEntryRecord& record, MemoryResource& resource)
	{
		using R = Result<void>;

		const SlangInt parameterCount = entry.getSpecializationParamCount();
		if (parameterCount == 0)
		{
			return R::Success();
		}

		auto unsupported = []()
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Entry specialization requires bounded type or uint32 generic parameters."));
		};

		if (parameterCount < 0 || parameterCount > static_cast<SlangInt>(MAXIMUM_SHADER_LINK_ARGUMENTS))
		{
			return unsupported();
		}

		slang::FunctionReflection* function = entry.getFunctionReflection();
		slang::GenericReflection* generic = function ? function->getGenericContainer() : nullptr;
		if (!generic ||
			generic->getOuterGenericContainer() ||
			generic->getTypeParameterCount() > MAXIMUM_SHADER_LINK_ARGUMENTS ||
			generic->getValueParameterCount() > MAXIMUM_SHADER_LINK_ARGUMENTS ||
			static_cast<SlangInt>(generic->getTypeParameterCount() + generic->getValueParameterCount()) != parameterCount)
		{
			return unsupported();
		}

		if (!record.SpecializationParameters.Reserve(static_cast<usize>(parameterCount)))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		slang::DeclReflection* declaration = generic->asDecl();
		if (!declaration || declaration->getChildrenCount() > 256)
		{
			return unsupported();
		}

		for (unsigned childIndex = 0; childIndex < declaration->getChildrenCount(); ++childIndex)
		{
			slang::DeclReflection* child = declaration->getChild(childIndex);
			const char* name = child ? child->getName() : nullptr;
			if (!name)
			{
				continue;
			}

			bool bKnown = false;
			EShaderLinkArgumentKind kind = EShaderLinkArgumentKind::Type;

			for (unsigned index = 0; index < generic->getTypeParameterCount(); ++index)
			{
				slang::VariableReflection* parameter = generic->getTypeParameter(index);
				if (parameter && parameter->getName() && CompareCString(name, parameter->getName()) == 0)
				{
					bKnown = true;
					break;
				}
			}

			for (unsigned index = 0; index < generic->getValueParameterCount(); ++index)
			{
				slang::VariableReflection* parameter = generic->getValueParameter(index);
				if (parameter && parameter->getName() && CompareCString(name, parameter->getName()) == 0)
				{
					if (bKnown ||
						!parameter->getType() ||
						parameter->getType()->getKind() != slang::TypeReflection::Kind::Scalar ||
						parameter->getType()->getScalarType() != slang::TypeReflection::UInt32)
					{
						return unsupported();
					}

					bKnown = true;
					kind = EShaderLinkArgumentKind::UInt32;
					break;
				}
			}

			if (!bKnown)
			{
				continue;
			}

			if (StringView(name).size() > 256)
			{
				return unsupported();
			}

			for (const ShaderModuleSpecializationParameter& previous : record.SpecializationParameters)
			{
				if (previous.Name.View() == StringView(name))
				{
					return unsupported();
				}
			}

			ShaderModuleSpecializationParameter copied(resource);
			copied.Kind = kind;
			if (!copied.Name.Assign(StringView(name)) || !record.SpecializationParameters.Add(Move(copied)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		if (record.SpecializationParameters.Size() != static_cast<usize>(parameterCount))
		{
			return unsupported();
		}

		return R::Success();
	}

	Result<Slang::ComPtr<slang::IComponentType>> SlangCompiler::specializeEntry(
		const ShaderLinkEntrySelection& selection,
		Span<slang::IModule* const> modules,
		slang::IComponentType& composite,
		ShaderBuildReport& report,
		const ShaderCompileRequest& request,
		MemoryResource& resource)
	{
		using R = Result<Slang::ComPtr<slang::IComponentType>>;

		if (selection.Arguments.Size() > MAXIMUM_SHADER_LINK_ARGUMENTS || composite.getSpecializationParamCount() != static_cast<SlangInt>(selection.Arguments.Size()))
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Composed program specialization does not match its entry schema."));
		}

		if (selection.Arguments.IsEmpty())
		{
			Slang::ComPtr<slang::IComponentType> retained(&composite);

			return R::Success(Move(retained));
		}

		Slang::ComPtr<slang::IBlob> diagnostics;
		Array<slang::SpecializationArg> arguments(resource);
		if (!arguments.Reserve(selection.Arguments.Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		char literals[MAXIMUM_SHADER_LINK_ARGUMENTS][12] = {};

		for (usize index = 0; index < selection.Arguments.Size(); ++index)
		{
			const ShaderLinkArgument& argument = selection.Arguments[index];
			if (argument.Kind == EShaderLinkArgumentKind::UInt32)
			{
				ToCharsResult encoded = ToChars(literals[index], literals[index] + 10, argument.UInt32Value);
				ASSERT(encoded.ec == EErrorCondition(), "Every uint32 literal fits its fixed decimal storage.");
				*encoded.ptr = 'u';
				arguments.AddRequired(slang::SpecializationArg::fromExpr(literals[index]));
				continue;
			}

			ASSERT(argument.Kind == EShaderLinkArgumentKind::Type && argument.TypeModuleIndex < modules.Size(), "Prepared type selections reference retained module roots.");
			slang::DeclReflection* declaration = modules[argument.TypeModuleIndex]->getModuleReflection();
			slang::TypeReflection* type = nullptr;
			if (declaration)
			{
				for (unsigned childIndex = 0; childIndex < declaration->getChildrenCount(); ++childIndex)
				{
					slang::DeclReflection* child = declaration->getChild(childIndex);
					if (child &&
						child->getKind() == slang::DeclReflection::Kind::Struct &&
						child->getName() &&
						argument.TypeName.View() == StringView(child->getName()))
					{
						if (type)
						{
							return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Selected type declaration is ambiguous."));
						}

						type = child->getType();
					}
				}
			}

			if (!type || type->getKind() != slang::TypeReflection::Kind::Struct || type->getGenericContainer())
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Type arguments require a concrete struct declared in the selected module."));
			}

			arguments.AddRequired(slang::SpecializationArg::fromType(type));
		}

		Slang::ComPtr<slang::IComponentType> specialized;
		const SlangResult result = composite.specialize(arguments.Data(), static_cast<SlangInt>(arguments.Size()), specialized.writeRef(), diagnostics.writeRef());
		if (diagnostics)
		{
			AddDiagnostics(report, {static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize()}, request);
		}

		if (SLANG_FAILED(result) || !specialized || specialized->getSpecializationParamCount() != 0)
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Typed entry specialization failed its compiler type constraints."));
		}

		return R::Success(Move(specialized));
	}
} // namespace rox::shader
