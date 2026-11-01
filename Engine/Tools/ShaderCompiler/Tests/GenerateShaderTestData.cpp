#include <ShaderCompiler.h>
#include <ShaderBuilder.h>
#include <SystemMemoryResource.h>
#include <Utility.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
	using namespace rox;
	using namespace core;

	class NoIncludes final : public shader::IShaderSourceResolver
	{
	public:
		Result<shader::ShaderSourceSnapshot> Resolve(StringView, StringView, MemoryResource&, CancellationToken) override
		{
			return Result<shader::ShaderSourceSnapshot>::Failure(EErrorCode::NotFound, "Shader.Test.Include", "Fixtures have no includes.");
		}
	};

	void addString(Array<String>& values, StringView value, MemoryResource& memory)
	{
		String text(memory);
		text.AssignRequired(value);
		values.AddRequired(Move(text));
	}

	bool generate(const std::filesystem::path& directory, const char* name, shader::IShaderCompiler& compiler, MemoryResource& memory)
	{
		std::ifstream file(directory / "Sources" / (std::string(name) + ".hlsl"), std::ios::binary);
		if (!file)
		{
			return false;
		}
		const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		shader::ShaderCompileRequest request(memory);
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		Result<shader::ShaderSourceSnapshot> snapshot = shader::CreateShaderSourceSnapshot(name, StringView(source.data(), source.size()), memory);
		if (!snapshot)
		{
			return false;
		}
		request.Root = Move(snapshot).TakeValue();
		const bool bSurface = std::string(name) == "Surface";
		const bool bCompute = std::string(name) == "Compute";
		const char* names[] = {bSurface ? "Vertex" : "Main", bSurface ? "Pixel" : "Fast"};
		for (uint32 i = 0; i < (bSurface || bCompute ? 2u : 1u); ++i)
		{
			shader::ShaderEntryPoint entry(memory);
			entry.Name.AssignRequired(names[i]);
			entry.Stage = bSurface ? (i == 0 ? shader::EShaderStage::Vertex : shader::EShaderStage::Pixel) : shader::EShaderStage::Compute;
			request.EntryPoints.AddRequired(Move(entry));
		}
		for (shader::EShaderTarget target : {shader::EShaderTarget::Dxil, shader::EShaderTarget::SpirV})
		{
			shader::ShaderTargetProfile profile;
			profile.Target = target;
			request.Targets.AddRequired(profile);
		}
		NoIncludes resolver;
		shader::ShaderBuildReport report(memory);
		Result<shader::ShaderBuildPlan> plan = compiler.PrepareBuild(request, resolver, report, memory);
		if (!plan)
		{
			std::cerr << plan.GetError().Message.CStr();
			return false;
		}
		Result<shader::ShaderBuildOutput> output = compiler.Compile(plan.Value(), report, memory);
		if (!output)
		{
			std::cerr << output.GetError().Message.CStr() << '\n';
			for (const shader::ShaderDiagnostic& diagnostic : report.Diagnostics)
			{
				std::cerr << diagnostic.Message.CStr() << '\n';
			}
			return false;
		}
		Result<shader::ShaderBuilder> cloned = shader::ShaderBuilder::FromShader(output.Value().Artifact, memory);
		if (!cloned)
		{
			return false;
		}
		shader::ShaderBuilder builder = Move(cloned).TakeValue();
		builder.Name.AssignRequired(name);
		if (bSurface)
		{
			shader::ShaderProperty property(memory);
			property.Name.AssignRequired("Albedo");
			property.DefaultValue.Type = shader::EShaderPropertyType::Texture2D;
			if (!builder.AddProperty(Move(property)))
			{
				return false;
			}
			for (shader::EShaderTarget target : {shader::EShaderTarget::Dxil, shader::EShaderTarget::SpirV})
			{
				shader::ShaderPropertyBinding binding(memory);
				binding.Property.AssignRequired("Albedo");
				binding.Entry.AssignRequired("Pixel");
				binding.Target.Target = target;
				binding.Binding.AssignRequired("Albedo");
				if (!builder.AddPropertyBinding(Move(binding)))
				{
					return false;
				}
			}
			shader::ShaderPass pass(memory);
			pass.Name.AssignRequired("Surface");
			shader::ShaderCodeChoice choice(memory);
			addString(choice.Entries, "Vertex", memory);
			addString(choice.Entries, "Pixel", memory);
			pass.Choices.AddRequired(Move(choice));
			if (!builder.AddPass(Move(pass)))
			{
				return false;
			}
		}
		else
		{
			shader::ShaderKernel kernel(memory);
			kernel.Name.AssignRequired("Simulate");
			shader::ShaderCodeChoice choice(memory);
			addString(choice.Entries, "Main", memory);
			kernel.Choices.AddRequired(Move(choice));
			if (bCompute)
			{
				if (!builder.AddKeyword("FAST"))
				{
					return false;
				}
				shader::ShaderCodeChoice fast(memory);
				addString(fast.Entries, "Fast", memory);
				addString(fast.Keywords, "FAST", memory);
				kernel.Choices.AddRequired(Move(fast));
			}
			if (!builder.AddKernel(Move(kernel)))
			{
				return false;
			}
		}
		Result<shader::Shader> asset = Move(builder).Build(memory);
		if (!asset)
		{
			std::cerr << asset.GetError().Message.CStr();
			return false;
		}
		Result<Array<Byte>> encoded = asset.Value().Save(memory);
		if (!encoded)
		{
			return false;
		}
		std::ofstream destination(directory / "Compiled" / (std::string(name) + ".shader"), std::ios::binary);
		destination.write(reinterpret_cast<const char*>(encoded.Value().Data()), static_cast<std::streamsize>(encoded.Value().Size()));
		destination.flush();
		std::cout << name << ": " << encoded.Value().Size() << " bytes\n";
		return destination.good();
	}
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		return 1;
	}
	MemoryResource& memory = SystemMemoryResource::Get();
	Result<InterfaceOwner<shader::IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
	if (!compiler)
	{
		std::cerr << compiler.GetError().Message.CStr();
		return 1;
	}
	for (const char* name : {"Compute", "Surface", "Parameters"})
	{
		if (!generate(argv[1], name, *compiler.Value(), memory))
		{
			return 1;
		}
	}
	return 0;
}
