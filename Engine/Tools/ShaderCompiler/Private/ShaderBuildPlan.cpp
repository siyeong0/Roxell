#include <ShaderBuildPlan.h>

#include "ShaderBuildInputWriter.h"
#include "ShaderCompilerInternal.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <ByteWriter.h>
#include <NumericLimits.hpp>
#include <Sha256.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		Result<void> validateInputs(const ShaderBuildPlan& plan, uint64 maximumBytes, CancellationToken cancellation)
		{
			ASSERT(plan.IsValid(), "Input encoding and verification require a valid shader build plan.");

			if (cancellation.IsCancellationRequested())
			{
				return Result<void>::Failure(EErrorCode::Cancelled, "ROX-SHADER-INPUT-CANCELLED", "Input verification cancelled.");
			}

			if (maximumBytes == 0 || maximumBytes > MAXIMUM_SHADER_BUILD_INPUT_BYTES || maximumBytes > NumericLimits<usize>::max())
			{
				return Result<void>::Failure(EErrorCode::InvalidArgument, "ROX-SHADER-INPUT-ARGUMENT", "Input plan or byte bound is invalid.");
			}

			return Result<void>::Success();
		}
	} // namespace

	struct ShaderBuildPlan::Storage
	{
		EShaderBuildPlanKind Kind = EShaderBuildPlanKind::SourceProgram;
		ShaderCompileRequest Request;
		SourceClosure Closure;
		ShaderToolchainIdentity Toolchain;
		Array<ShaderDependency> Dependencies;
		Hash256 Identity;

		Array<uint32> SourceOrder;
		Array<uint32> AliasOrder;
		Array<uint32> EdgeOrder;
		Array<uint32> DefineOrder;
		Array<uint32> EntryOrder;
		Array<uint32> TargetOrder;

		ShaderLinkRequest LinkRequest;
		Array<ShaderModuleArtifact> Modules;

		Storage(IShaderSourceResolver& resolver, MemoryResource& resource, CancellationToken cancellation) noexcept
			: Request(resource)
			, Closure(Request, resolver, resource, cancellation)
			, Toolchain(resource)
			, Dependencies(resource)
			, SourceOrder(resource)
			, AliasOrder(resource)
			, EdgeOrder(resource)
			, DefineOrder(resource)
			, EntryOrder(resource)
			, TargetOrder(resource)
			, LinkRequest(resource)
			, Modules(resource)
		{
		}

		explicit Storage(MemoryResource& resource) noexcept
			: Kind(EShaderBuildPlanKind::LinkedProgram)
			, Request(resource)
			, Closure(Request, resource)
			, Toolchain(resource)
			, Dependencies(resource)
			, SourceOrder(resource)
			, AliasOrder(resource)
			, EdgeOrder(resource)
			, DefineOrder(resource)
			, EntryOrder(resource)
			, TargetOrder(resource)
			, LinkRequest(resource)
			, Modules(resource)
		{
		}
	};

	ShaderBuildPlan::ShaderBuildPlan() noexcept = default;
	ShaderBuildPlan::~ShaderBuildPlan() = default;
	ShaderBuildPlan::ShaderBuildPlan(const ShaderBuildPlan&) noexcept = default;
	ShaderBuildPlan& ShaderBuildPlan::operator=(const ShaderBuildPlan&) noexcept = default;
	ShaderBuildPlan::ShaderBuildPlan(ShaderBuildPlan&&) noexcept = default;
	ShaderBuildPlan& ShaderBuildPlan::operator=(ShaderBuildPlan&&) noexcept = default;

	bool ShaderBuildPlan::IsValid() const noexcept
	{
		return static_cast<bool>(m_Storage);
	}

	EShaderBuildPlanKind ShaderBuildPlan::GetKind() const noexcept
	{
		ASSERT(IsValid(), "Plan kind inspection requires a valid plan.");
		return m_Storage->Kind;
	}

	const ShaderLinkRequest& ShaderBuildPlan::GetLinkRequest() const noexcept
	{
		ASSERT(IsValid() && GetKind() == EShaderBuildPlanKind::LinkedProgram, "Link inspection requires a linked plan.");
		return m_Storage->LinkRequest;
	}

	Span<const ShaderModuleArtifact> ShaderBuildPlan::GetModuleArtifacts() const noexcept
	{
		return m_Storage ? m_Storage->Modules.View() : Span<const ShaderModuleArtifact>{};
	}

	Hash256 ShaderBuildPlan::GetBuildIdentity() const noexcept
	{
		return m_Storage ? m_Storage->Identity : Hash256{};
	}

	const ShaderToolchainIdentity& ShaderBuildPlan::GetToolchainIdentity() const noexcept
	{
		ASSERT(IsValid(), "Toolchain inspection requires a valid shader build plan.");
		return m_Storage->Toolchain;
	}

	const ShaderCompileRequest& ShaderBuildPlan::GetRequest() const noexcept
	{
		ASSERT(IsValid(), "Request inspection requires a valid shader build plan.");
		return m_Storage->Request;
	}

	Span<const ShaderSourceSnapshot> ShaderBuildPlan::GetSources() const noexcept
	{
		return m_Storage ? m_Storage->Closure.GetSources().View() : Span<const ShaderSourceSnapshot>{};
	}

	Span<const ShaderDependency> ShaderBuildPlan::GetDependencies() const noexcept
	{
		return m_Storage ? m_Storage->Dependencies.View() : Span<const ShaderDependency>{};
	}

	const ShaderSourceSnapshot* ShaderBuildPlan::FindSource(StringView identityOrAlias) const noexcept
	{
		ASSERT(IsValid() && GetKind() != EShaderBuildPlanKind::LinkedProgram, "Source resolution requires a captured source plan.");
		return m_Storage->Closure.Find(identityOrAlias);
	}

	ShaderIncludeResolution ShaderBuildPlan::ResolveInclude(StringView requester, StringView requestedPath) const noexcept
	{
		const ShaderSourceSnapshot* source = FindSource(requester);
		if (source == nullptr)
		{
			return {};
		}

		const Span<const ShaderSourceSnapshot> sources = GetSources();
		const uint32 requesterIndex = static_cast<uint32>(source - sources.Data());

		for (const SourceIncludeEdge& edge : m_Storage->Closure.GetIncludeEdges())
		{
			if (edge.Requester == requesterIndex && edge.RequestedPath.View() == requestedPath)
			{
				return edge.SourceIndex == NO_SHADER_SOURCE ? ShaderIncludeResolution{EShaderIncludeResolution::Missing, nullptr}
															: ShaderIncludeResolution{EShaderIncludeResolution::Resolved, &sources[edge.SourceIndex]};
			}
		}

		return {};
	}

	usize ShaderBuildPlan::GetSourceAliasCount() const noexcept
	{
		ASSERT(IsValid() && GetKind() != EShaderBuildPlanKind::LinkedProgram, "Alias inspection requires a captured source plan.");
		return m_Storage->Closure.GetAliases().Size();
	}

	ShaderSourceAliasView ShaderBuildPlan::GetSourceAlias(usize index) const noexcept
	{
		ASSERT(index < GetSourceAliasCount(), "Source alias index must belong to this plan.");
		const SourceAlias& alias = m_Storage->Closure.GetAliases()[index];

		return {alias.Path.View(), alias.SourceIndex};
	}

	usize ShaderBuildPlan::GetSourceIncludeCount() const noexcept
	{
		ASSERT(IsValid() && GetKind() != EShaderBuildPlanKind::LinkedProgram, "Include inspection requires a captured source plan.");
		return m_Storage->Closure.GetIncludeEdges().Size();
	}

	ShaderSourceIncludeView ShaderBuildPlan::GetSourceInclude(usize index) const noexcept
	{
		ASSERT(index < GetSourceIncludeCount(), "Source include index must belong to this plan.");
		static_assert(NO_CAPTURED_SHADER_SOURCE == NO_SHADER_SOURCE);
		const SourceIncludeEdge& edge = m_Storage->Closure.GetIncludeEdges()[index];

		return {edge.Requester, edge.SourceIndex, edge.RequestedPath.View()};
	}

	uint64 ShaderBuildPlan::GetRetainedByteCount() const noexcept
	{
		ASSERT(IsValid(), "Retained byte inspection requires a valid plan.");
		const ShaderBuildPlan::Storage& storage = *m_Storage;
		uint64 bytes = sizeof(Storage);
		auto strings = [&](const String& value)
		{
			bytes += value.Capacity() + 1;
		};

		const ShaderCompileRequest& request = storage.Request;
		strings(request.Root.Identity);
		strings(request.Root.Source);
		strings(storage.Toolchain.SdkIdentity);
		strings(storage.Toolchain.CompilerVersion);
		bytes += request.EntryPoints.Capacity() * sizeof(shader::ShaderEntryPoint);
		bytes += request.Targets.Capacity() * sizeof(shader::ShaderTargetProfile);
		bytes += request.Defines.Capacity() * sizeof(ShaderDefine);
		bytes += request.IncludeRoots.Capacity() * sizeof(String);
		bytes += request.LocalRootRecipes.Capacity() * sizeof(ShaderLocalRootRecipe);

		for (const ShaderLocalRootRecipe& recipe : request.LocalRootRecipes)
		{
			strings(recipe.EntryPointName);
			bytes += recipe.Arguments.Capacity() * sizeof(ShaderLocalRootArgumentDeclaration);

			for (const ShaderLocalRootArgumentDeclaration& argument : recipe.Arguments)
			{
				strings(argument.BindingName);
			}
		}

		for (const ShaderEntryPoint& entry : request.EntryPoints)
		{
			strings(entry.Name);
		}

		for (const ShaderDefine& define : request.Defines)
		{
			strings(define.Name);
			strings(define.Value);
		}

		for (const String& root : request.IncludeRoots)
		{
			strings(root);
		}

		bytes += storage.Dependencies.Capacity() * sizeof(ShaderDependency);

		for (const ShaderDependency& dependency : storage.Dependencies)
		{
			strings(dependency.Identity);
		}

		bytes += storage.Closure.GetOwnedByteCount();
		bytes += storage.Modules.Capacity() * sizeof(ShaderModuleArtifact);
		bytes += storage.LinkRequest.EntryPoints.Capacity() * sizeof(ShaderLinkEntrySelection);
		bytes += storage.LinkRequest.Targets.Capacity() * sizeof(shader::ShaderTargetProfile);

		for (const ShaderLinkEntrySelection& entry : storage.LinkRequest.EntryPoints)
		{
			strings(entry.Name);
			bytes += entry.Arguments.Capacity() * sizeof(ShaderLinkArgument);

			for (const ShaderLinkArgument& argument : entry.Arguments)
			{
				strings(argument.TypeName);
			}
		}

		for (const ShaderModuleArtifact& module : storage.Modules)
		{
			const uint64 moduleCharge = module.GetRetainedByteCount();
			if (moduleCharge > NumericLimits<uint64>::max() - bytes)
			{
				return NumericLimits<uint64>::max();
			}

			bytes += moduleCharge;
		}

		bytes += (storage.SourceOrder.Capacity() + storage.AliasOrder.Capacity() + storage.EdgeOrder.Capacity() + storage.DefineOrder.Capacity() + storage.EntryOrder.Capacity() +
					storage.TargetOrder.Capacity()) *
			sizeof(uint32);

		return bytes;
	}

	bool ShaderBuildPlan::CanShareAcrossThreads() const noexcept
	{
		if (!IsValid())
		{
			return false;
		}

		if (m_Storage.GetResource()->GetThreadSafety() != EMemoryResourceThreadSafety::ThreadSafe)
		{
			return false;
		}

		for (const ShaderModuleArtifact& module : m_Storage->Modules)
		{
			if (!module.CanShareAcrossThreads())
			{
				return false;
			}
		}

		return true;
	}

	void ShaderBuildPlan::writeInputsRecursive(ShaderBuildInputWriter& writer, uint32 depth) const
	{
		ASSERT(IsValid() && depth <= 1, "Input proofs require a valid bounded immutable plan tree.");

		if (!writer.IsValid())
		{
			return;
		}

		const ShaderBuildPlan::Storage& storage = *m_Storage;
		const ShaderCompileRequest& request = storage.Request;
		const ShaderToolchainIdentity& toolchain = storage.Toolchain;
		writer.Text("ROX-SHADER-INPUTS");
		writer.U32(static_cast<uint32>(storage.Kind));
		writer.U32(static_cast<uint32>(toolchain.Compiler));
		writer.U32(toolchain.AdapterVersion);
		writer.U32(toolchain.ShaderAbiVersion);
		writer.Text(toolchain.SdkIdentity);
		writer.Text(toolchain.CompilerVersion);
		writer.U32(static_cast<uint32>(request.Language));
		writer.U32(request.LanguageVersion);
		writer.U32(request.EngineProfileVersion);
		writer.U32(static_cast<uint32>(request.Optimization));
		writer.U32(static_cast<uint32>(request.DebugInfo));
		writer.U32(static_cast<uint32>(request.MatrixOrder));
		writer.U32(request.bWarningsAsErrors ? 1 : 0);
		writer.U32(request.bPreciseFloatingPoint ? 1 : 0);
		{
			writer.U32(request.RayDeclaredLimits.MaximumPayloadBytes);
			writer.U32(request.RayDeclaredLimits.MaximumAttributeBytes);
			writer.U32(request.RayDeclaredLimits.MaximumCallableBytes);
		}

		{
			writer.U32(static_cast<uint32>(request.LocalRootRecipes.Size()));

			for (const ShaderLocalRootRecipe& recipe : request.LocalRootRecipes)
			{
				writer.Text(recipe.EntryPointName);
				writer.U32(static_cast<uint32>(recipe.Arguments.Size()));

				for (const ShaderLocalRootArgumentDeclaration& argument : recipe.Arguments)
				{
					writer.U32(static_cast<uint32>(argument.Kind));
					writer.Text(argument.BindingName);
					writer.U32(argument.ValueCount);
				}
			}
		}

		if (storage.Kind == EShaderBuildPlanKind::LinkedProgram)
		{
			writer.U64(storage.Modules.Size());

			for (const ShaderModuleArtifact& artifact : storage.Modules)
			{
				if (!writer.IsValid())
				{
					return;
				}

				const ShaderModuleArtifact& module = artifact;
				ASSERT(module.GetSourcePlan().GetKind() == EShaderBuildPlanKind::SourceModule, "An immutable module retains a source module plan, never a linked plan.");
				module.GetSourcePlan().writeInputsRecursive(writer, depth + 1);
				writer.Hash(module.GetModuleIdentity());
				writer.U32(module.GetRootModuleIndex());
				writer.U64(module.GetModules().Size());

				for (usize index = 0; index < module.GetModules().Size(); ++index)
				{
					const ShaderModuleRecord& record = module.GetModules()[index];
					writer.Text(record.Name);
					writer.Text(record.SourceIdentity);
					writer.Hash(record.IRHash);
					writer.U64(record.ByteCount);
					writer.Bytes(module.GetModuleBytes(static_cast<uint32>(index)));
				}

				writer.U64(module.GetEntryPoints().Size());

				for (const ShaderModuleEntryRecord& entry : module.GetEntryPoints())
				{
					writer.U32(entry.ModuleIndex);
					writer.Text(entry.Name);
					writer.U32(static_cast<uint32>(entry.Stage));
					writer.U64(entry.SpecializationParameters.Size());

					for (const ShaderModuleSpecializationParameter& parameter : entry.SpecializationParameters)
					{
						writer.U32(static_cast<uint32>(parameter.Kind));
						writer.Text(parameter.Name);
					}
				}
			}

			writer.U64(storage.LinkRequest.EntryPoints.Size());

			for (const ShaderLinkEntrySelection& entry : storage.LinkRequest.EntryPoints)
			{
				writer.U32(entry.ModuleIndex);
				writer.Text(entry.Name);
				writer.U32(static_cast<uint32>(entry.Stage));
				writer.U64(entry.Arguments.Size());

				for (const ShaderLinkArgument& argument : entry.Arguments)
				{
					writer.U32(static_cast<uint32>(argument.Kind));
					writer.U32(argument.TypeModuleIndex);
					writer.Text(argument.TypeName);
					writer.U32(argument.UInt32Value);
				}
			}

			writer.U64(storage.LinkRequest.Targets.Size());

			for (const ShaderTargetProfile& target : storage.LinkRequest.Targets)
			{
				writer.Target(target);
			}

			return;
		}

		writer.Text(request.Root.Identity);
		writer.U64(request.IncludeRoots.Size());

		for (const String& root : request.IncludeRoots)
		{
			writer.Text(root);
		}

		writer.U64(storage.DefineOrder.Size());

		for (uint32 const index : storage.DefineOrder)
		{
			writer.Text(request.Defines[index].Name);
			writer.Text(request.Defines[index].Value);
		}

		writer.U64(storage.EntryOrder.Size());

		for (uint32 const index : storage.EntryOrder)
		{
			const ShaderEntryPoint& entry = request.EntryPoints[index];
			writer.Text(entry.Name);
			writer.U32(static_cast<uint32>(entry.Stage));

			for (const uint32 dimension : entry.ThreadGroupSize)
			{
				writer.U32(dimension);
			}
		}

		writer.U64(storage.TargetOrder.Size());

		for (uint32 const index : storage.TargetOrder)
		{
			writer.Target(request.Targets[index]);
		}

		const SourceClosure& closure = storage.Closure;
		writer.U64(storage.SourceOrder.Size());

		for (uint32 const index : storage.SourceOrder)
		{
			const ShaderSourceSnapshot& source = closure.GetSources()[index];
			writer.Text(source.Identity);
			writer.Text(source.Source);
			writer.Hash(source.ContentHash);
		}

		auto sourceIdentity = [&](uint32 index)
		{
			writer.U32(index == NO_SHADER_SOURCE ? 0 : 1);

			if (index != NO_SHADER_SOURCE)
			{
				writer.Text(closure.GetSources()[index].Identity);
			}
		};

		writer.U64(storage.AliasOrder.Size());

		for (uint32 const index : storage.AliasOrder)
		{
			const SourceAlias& alias = closure.GetAliases()[index];
			writer.Text(alias.Path);
			sourceIdentity(alias.SourceIndex);
		}

		writer.U64(storage.EdgeOrder.Size());

		for (uint32 const index : storage.EdgeOrder)
		{
			const SourceIncludeEdge& edge = closure.GetIncludeEdges()[index];
			sourceIdentity(edge.Requester);
			writer.Text(edge.RequestedPath);
			sourceIdentity(edge.SourceIndex);
		}
	}

	bool ShaderBuildPlan::HasSameInputs(const ShaderBuildPlan& other) const noexcept
	{
		if (!IsValid() || !other.IsValid())
		{
			return IsValid() == other.IsValid();
		}

		if (m_Storage.Get() == other.m_Storage.Get())
		{
			return true;
		}

		const ShaderBuildPlan::Storage& left = *m_Storage;
		const ShaderBuildPlan::Storage& right = *other.m_Storage;
		const ShaderCompileRequest& a = left.Request;
		const ShaderCompileRequest& b = right.Request;
		if (left.Kind != right.Kind ||
			!(left.Toolchain == right.Toolchain) ||
			a.Language != b.Language ||
			a.LanguageVersion != b.LanguageVersion ||
			a.EngineProfileVersion != b.EngineProfileVersion ||
			a.Optimization != b.Optimization ||
			a.DebugInfo != b.DebugInfo ||
			a.MatrixOrder != b.MatrixOrder ||
			a.bWarningsAsErrors != b.bWarningsAsErrors ||
			a.bPreciseFloatingPoint != b.bPreciseFloatingPoint ||
			a.RayDeclaredLimits != b.RayDeclaredLimits ||
			a.Root.Identity != b.Root.Identity ||
			a.Root.Source != b.Root.Source ||
			a.Root.ContentHash != b.Root.ContentHash ||
			a.Limits.MaximumSources != b.Limits.MaximumSources ||
			a.Limits.MaximumSourceBytes != b.Limits.MaximumSourceBytes ||
			a.Limits.MaximumAggregateSourceBytes != b.Limits.MaximumAggregateSourceBytes ||
			a.Limits.MaximumTraversalDepth != b.Limits.MaximumTraversalDepth ||
			a.Limits.MaximumDiagnostics != b.Limits.MaximumDiagnostics ||
			a.Limits.MaximumDiagnosticBytes != b.Limits.MaximumDiagnosticBytes ||
			a.Limits.MaximumModules != b.Limits.MaximumModules ||
			a.Limits.MaximumModuleBytes != b.Limits.MaximumModuleBytes ||
			a.Limits.MaximumAggregateModuleBytes != b.Limits.MaximumAggregateModuleBytes)
		{
			return false;
		}

		if (a.LocalRootRecipes.Size() != b.LocalRootRecipes.Size())
		{
			return false;
		}

		for (usize index = 0; index < a.LocalRootRecipes.Size(); ++index)
		{
			const ShaderLocalRootRecipe& leftRecipe = a.LocalRootRecipes[index];
			const ShaderLocalRootRecipe& rightRecipe = b.LocalRootRecipes[index];
			if (leftRecipe.EntryPointName != rightRecipe.EntryPointName || leftRecipe.Arguments.Size() != rightRecipe.Arguments.Size())
			{
				return false;
			}

			for (usize argument = 0; argument < leftRecipe.Arguments.Size(); ++argument)
			{
				const ShaderLocalRootArgumentDeclaration& leftArgument = leftRecipe.Arguments[argument];
				const ShaderLocalRootArgumentDeclaration& rightArgument = rightRecipe.Arguments[argument];
				if (leftArgument.Kind != rightArgument.Kind || leftArgument.BindingName != rightArgument.BindingName || leftArgument.ValueCount != rightArgument.ValueCount)
				{
					return false;
				}
			}
		}

		if (left.Kind == EShaderBuildPlanKind::LinkedProgram)
		{
			if (left.Modules.Size() != right.Modules.Size() || left.LinkRequest.EntryPoints.Size() != right.LinkRequest.EntryPoints.Size() || a.Targets.Size() != b.Targets.Size())
			{
				return false;
			}

			for (usize index = 0; index < a.Targets.Size(); ++index)
			{
				if (a.Targets[index] != b.Targets[index])
				{
					return false;
				}
			}

			for (usize index = 0; index < left.LinkRequest.EntryPoints.Size(); ++index)
			{
				const ShaderLinkEntrySelection& x = left.LinkRequest.EntryPoints[index];
				const ShaderLinkEntrySelection& y = right.LinkRequest.EntryPoints[index];
				if (x.ModuleIndex != y.ModuleIndex || x.Name != y.Name || x.Stage != y.Stage || x.Arguments.Size() != y.Arguments.Size())
				{
					return false;
				}

				for (usize argumentIndex = 0; argumentIndex < x.Arguments.Size(); ++argumentIndex)
				{
					const ShaderLinkArgument& leftArgument = x.Arguments[argumentIndex];
					const ShaderLinkArgument& rightArgument = y.Arguments[argumentIndex];
					if (leftArgument.Kind != rightArgument.Kind ||
						leftArgument.TypeModuleIndex != rightArgument.TypeModuleIndex ||
						leftArgument.TypeName != rightArgument.TypeName ||
						leftArgument.UInt32Value != rightArgument.UInt32Value)
					{
						return false;
					}
				}
			}

			for (usize index = 0; index < left.Modules.Size(); ++index)
			{
				if (!left.Modules[index].HasSameInputs(right.Modules[index]))
				{
					return false;
				}
			}

			return true;
		}

		auto compare = [](const Array<uint32>& leftOrder, const Array<uint32>& rightOrder, auto equal)
		{
			if (leftOrder.Size() != rightOrder.Size())
			{
				return false;
			}

			for (usize i = 0; i < leftOrder.Size(); ++i)
			{
				if (!equal(leftOrder[i], rightOrder[i]))
				{
					return false;
				}
			}

			return true;
		};

		if (!compare(left.DefineOrder, right.DefineOrder, [&](uint32 x, uint32 y) { return a.Defines[x].Name == b.Defines[y].Name && a.Defines[x].Value == b.Defines[y].Value; }) ||
			!compare(left.TargetOrder, right.TargetOrder, [&](uint32 x, uint32 y) { return a.Targets[x] == b.Targets[y]; }) ||
			!compare(
				left.EntryOrder, right.EntryOrder,
				[&](uint32 x, uint32 y)
				{
					const ShaderEntryPoint& entryA = a.EntryPoints[x];
					const ShaderEntryPoint& entryB = b.EntryPoints[y];

					return entryA.Name == entryB.Name && entryA.Stage == entryB.Stage &&
						Equal(Begin(entryA.ThreadGroupSize), End(entryA.ThreadGroupSize), Begin(entryB.ThreadGroupSize));
				}) ||
			a.IncludeRoots.Size() != b.IncludeRoots.Size())
		{
			return false;
		}

		for (usize i = 0; i < a.IncludeRoots.Size(); ++i)
		{
			if (a.IncludeRoots[i] != b.IncludeRoots[i])
			{
				return false;
			}
		}

		const SourceClosure& closureA = left.Closure;
		const SourceClosure& closureB = right.Closure;
		auto sourceName = [](const SourceClosure& closure, uint32 index)
		{
			return index == NO_SHADER_SOURCE ? StringView{} : closure.GetSources()[index].Identity.View();
		};

		return compare(
				left.SourceOrder, right.SourceOrder,
				[&](uint32 x, uint32 y)
				{
					const ShaderSourceSnapshot& sourceA = closureA.GetSources()[x];
					const ShaderSourceSnapshot& sourceB = closureB.GetSources()[y];

					return sourceA.Identity == sourceB.Identity && sourceA.Source == sourceB.Source && sourceA.ContentHash == sourceB.ContentHash;
				}) &&
			compare(
				left.AliasOrder, right.AliasOrder,
				[&](uint32 x, uint32 y)
				{
					const SourceAlias& aliasA = closureA.GetAliases()[x];
					const SourceAlias& aliasB = closureB.GetAliases()[y];

					return aliasA.Path == aliasB.Path && sourceName(closureA, aliasA.SourceIndex) == sourceName(closureB, aliasB.SourceIndex);
				}) &&
			compare(
				left.EdgeOrder, right.EdgeOrder,
				[&](uint32 x, uint32 y)
				{
					const SourceIncludeEdge& edgeA = closureA.GetIncludeEdges()[x];
					const SourceIncludeEdge& edgeB = closureB.GetIncludeEdges()[y];

					return sourceName(closureA, edgeA.Requester) == sourceName(closureB, edgeB.Requester) && edgeA.RequestedPath == edgeB.RequestedPath &&
						sourceName(closureA, edgeA.SourceIndex) == sourceName(closureB, edgeB.SourceIndex);
				});
	}

	Result<ShaderBuildPlan> ShaderBuildPlan::Capture(
		const IShaderCompiler& compiler,
		const ShaderCompileRequest& input,
		IShaderSourceResolver& resolver,
		MemoryResource& resource,
		CancellationToken cancellation)
	{
		using R = Result<ShaderBuildPlan>;

		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		Result<void> validated = ValidateRequest(input, compiler.GetToolchainIdentity().Compiler);
		if (!validated)
		{
			return R::Failure(Move(validated.GetError()));
		}

		Result<void> supported = ValidateProfiles(input, compiler.GetProfiles(), compiler.GetToolchainIdentity().ShaderAbiVersion);
		if (!supported)
		{
			return R::Failure(Move(supported.GetError()));
		}

		ShaderBuildPlan plan;
		if (!MakeSharedInto(resource, &plan.m_Storage, resolver, resource, cancellation))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		ShaderBuildPlan::Storage& storage = *plan.m_Storage;
		ShaderCompileRequest& request = storage.Request;
		storage.Kind = input.Purpose == EShaderSourceBuildPurpose::Module ? EShaderBuildPlanKind::SourceModule : EShaderBuildPlanKind::SourceProgram;
		request.Purpose = input.Purpose;
		request.Language = input.Language;
		request.Optimization = input.Optimization;
		request.DebugInfo = input.DebugInfo;
		request.MatrixOrder = input.MatrixOrder;
		request.bWarningsAsErrors = input.bWarningsAsErrors;
		request.bPreciseFloatingPoint = input.bPreciseFloatingPoint;
		request.LanguageVersion = input.LanguageVersion;
		request.EngineProfileVersion = input.EngineProfileVersion;
		request.Limits = input.Limits;
		request.RayDeclaredLimits = input.RayDeclaredLimits;
		request.Root.ContentHash = input.Root.ContentHash;
		if (!request.Root.Identity.Assign(input.Root.Identity) ||
			!request.Root.Source.Assign(input.Root.Source) ||
			!request.EntryPoints.Reserve(input.EntryPoints.Size()) ||
			!request.Targets.Reserve(input.Targets.Size()) ||
			!request.Defines.Reserve(input.Defines.Size()) ||
			!request.IncludeRoots.Reserve(input.IncludeRoots.Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (const ShaderEntryPoint& sourceEntry : input.EntryPoints)
		{
			shader::ShaderEntryPoint entry(resource);
			entry.Stage = sourceEntry.Stage;
			Copy(Begin(sourceEntry.ThreadGroupSize), End(sourceEntry.ThreadGroupSize), entry.ThreadGroupSize);
			if (!entry.Name.Assign(sourceEntry.Name) || !request.EntryPoints.Add(Move(entry)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		for (ShaderTargetProfile const target : input.Targets)
		{
			const bool bAdded = request.Targets.Add(target);
			ASSERT(bAdded, "Requested target storage is reserved before plan publication.");
		}

		for (const ShaderDefine& sourceDefine : input.Defines)
		{
			ShaderDefine define(resource);
			if (!define.Name.Assign(sourceDefine.Name) || !define.Value.Assign(sourceDefine.Value) || !request.Defines.Add(Move(define)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		for (const String& sourceRoot : input.IncludeRoots)
		{
			String root(resource);
			if (!root.Assign(sourceRoot) || !request.IncludeRoots.Add(Move(root)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		for (const ShaderLocalRootRecipe& sourceRecipe : input.LocalRootRecipes)
		{
			ShaderLocalRootRecipe recipe(resource);
			if (!recipe.EntryPointName.Assign(sourceRecipe.EntryPointName) || !recipe.Arguments.Reserve(sourceRecipe.Arguments.Size()))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			for (const ShaderLocalRootArgumentDeclaration& sourceArgument : sourceRecipe.Arguments)
			{
				ShaderLocalRootArgumentDeclaration argument(resource);
				argument.Kind = sourceArgument.Kind;
				argument.ValueCount = sourceArgument.ValueCount;
				if (!argument.BindingName.Assign(sourceArgument.BindingName) || !recipe.Arguments.Add(Move(argument)))
				{
					return R::Failure(MakeBuildAllocationError());
				}
			}

			if (!request.LocalRootRecipes.Add(Move(recipe)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		const ShaderToolchainIdentity& toolchain = compiler.GetToolchainIdentity();
		storage.Toolchain.Compiler = toolchain.Compiler;
		storage.Toolchain.AdapterVersion = toolchain.AdapterVersion;
		storage.Toolchain.ShaderAbiVersion = toolchain.ShaderAbiVersion;
		if (!storage.Toolchain.SdkIdentity.Assign(toolchain.SdkIdentity) || !storage.Toolchain.CompilerVersion.Assign(toolchain.CompilerVersion))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		Result<void> captured = storage.Closure.Initialize();
		if (!captured)
		{
			return R::Failure(Move(captured.GetError()));
		}

		ShaderBuildOutput identity(resource);
		Result<void> hashed = PrepareBuildIdentity(identity, request, storage.Closure, storage.Toolchain, resource, cancellation);
		if (!hashed)
		{
			return R::Failure(Move(hashed.GetError()));
		}

		storage.Identity = identity.BuildIdentity;
		storage.Dependencies = Move(identity.Dependencies);
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		storage.Closure.Freeze();
		auto order = [](Array<uint32>& indices, const auto& rows, auto less)
		{
			if (!indices.Resize(rows.Size()))
			{
				return false;
			}

			for (uint32 i = 0; i < indices.Size(); ++i)
			{
				indices[i] = i;
			}

			if (indices.Size() > 1)
			{
				Sort(indices.begin(), indices.end(), less);
			}

			return true;
		};

		const SourceClosure& closure = storage.Closure;
		if (!order(
				storage.SourceOrder, closure.GetSources(), [&](uint32 x, uint32 y) { return closure.GetSources()[x].Identity.View() < closure.GetSources()[y].Identity.View(); }) ||
			!order(storage.AliasOrder, closure.GetAliases(), [&](uint32 x, uint32 y) { return closure.GetAliases()[x].Path.View() < closure.GetAliases()[y].Path.View(); }) ||
			!order(
				storage.EdgeOrder, closure.GetIncludeEdges(),
				[&](uint32 x, uint32 y)
				{
					const SourceIncludeEdge& edgeA = closure.GetIncludeEdges()[x];
					const SourceIncludeEdge& edgeB = closure.GetIncludeEdges()[y];
					const StringView nameA = closure.GetSources()[edgeA.Requester].Identity.View();
					const StringView nameB = closure.GetSources()[edgeB.Requester].Identity.View();

					return nameA == nameB ? edgeA.RequestedPath.View() < edgeB.RequestedPath.View() : nameA < nameB;
				}) ||
			!order(storage.DefineOrder, request.Defines, [&](uint32 x, uint32 y) { return request.Defines[x].Name.View() < request.Defines[y].Name.View(); }) ||
			!order(storage.EntryOrder, request.EntryPoints, [&](uint32 x, uint32 y) { return request.EntryPoints[x].Name.View() < request.EntryPoints[y].Name.View(); }) ||
			!order(storage.TargetOrder, request.Targets, [&](uint32 x, uint32 y) { return request.Targets[x] < request.Targets[y]; }))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		return R::Success(Move(plan));
	}

	Result<ShaderBuildPlan> ShaderBuildPlan::CaptureLink(
		const IShaderCompiler& compiler,
		const ShaderLinkRequest& input,
		Span<const ShaderModuleArtifact> modules,
		MemoryResource& resource,
		CancellationToken cancellation)
	{
		using R = Result<ShaderBuildPlan>;

		auto invalid = []()
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Link request, module selection or bounded options are invalid."));
		};

		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		if (modules.IsEmpty() ||
			modules.Size() > input.Limits.MaximumModules ||
			input.Limits.MaximumModules > 1024 ||
			!input.Limits.MaximumModuleBytes ||
			input.Limits.MaximumModuleBytes > 64u * 1024u * 1024u ||
			!input.Limits.MaximumAggregateModuleBytes ||
			input.Limits.MaximumAggregateModuleBytes > 1024ull * 1024ull * 1024ull ||
			input.EntryPoints.IsEmpty() ||
			input.EntryPoints.Size() > 256 ||
			input.Targets.IsEmpty() ||
			input.Targets.Size() > 2 ||
			input.Optimization > EShaderOptimization::Maximum ||
			input.DebugInfo > EShaderDebugInfo::Standard ||
			(input.MatrixOrder != shader::EShaderMatrixOrder::RowMajor && input.MatrixOrder != shader::EShaderMatrixOrder::ColumnMajor))
		{
			return invalid();
		}

		if (compiler.GetToolchainIdentity().Compiler != EShaderCompiler::Slang || compiler.GetToolchainIdentity().ShaderAbiVersion != shader::SHADER_ABI_VERSION)
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Module linking requires the current Slang module adapter."));
		}

		ShaderBuildPlan plan;
		if (!MakeSharedInto(resource, &plan.m_Storage, resource))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		ShaderBuildPlan::Storage& storage = *plan.m_Storage;
		ShaderCompileRequest& request = storage.Request;
		request.Language = EShaderSourceLanguage::Slang;
		request.Optimization = input.Optimization;
		request.DebugInfo = input.DebugInfo;
		request.MatrixOrder = input.MatrixOrder;
		request.bWarningsAsErrors = input.bWarningsAsErrors;
		request.bPreciseFloatingPoint = input.bPreciseFloatingPoint;
		request.Limits = input.Limits;

		storage.LinkRequest.Optimization = input.Optimization;
		storage.LinkRequest.DebugInfo = input.DebugInfo;
		storage.LinkRequest.MatrixOrder = input.MatrixOrder;
		storage.LinkRequest.bWarningsAsErrors = input.bWarningsAsErrors;
		storage.LinkRequest.bPreciseFloatingPoint = input.bPreciseFloatingPoint;
		storage.LinkRequest.Limits = input.Limits;
		storage.Toolchain.Compiler = compiler.GetToolchainIdentity().Compiler;
		storage.Toolchain.AdapterVersion = compiler.GetToolchainIdentity().AdapterVersion;
		storage.Toolchain.ShaderAbiVersion = compiler.GetToolchainIdentity().ShaderAbiVersion;
		if (!storage.Toolchain.SdkIdentity.Assign(compiler.GetToolchainIdentity().SdkIdentity) ||
			!storage.Toolchain.CompilerVersion.Assign(compiler.GetToolchainIdentity().CompilerVersion) ||
			!storage.Modules.Reserve(modules.Size()) ||
			!request.EntryPoints.Reserve(input.EntryPoints.Size()) ||
			!storage.LinkRequest.EntryPoints.Reserve(input.EntryPoints.Size()) ||
			!request.Targets.Reserve(input.Targets.Size()) ||
			!storage.LinkRequest.Targets.Reserve(input.Targets.Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		uint64 aggregateBytes = 0;
		usize aggregateModules = 0;
		usize aggregateSources = 0;
		uint64 aggregateSourceBytes = 0;

		for (usize index = 0; index < modules.Size(); ++index)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const ShaderModuleArtifact& artifact = modules[index];
			if (!artifact.IsValid() || !(artifact.GetToolchainIdentity() == storage.Toolchain))
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Module IR toolchain identity does not match the link compiler."));
			}

			const ShaderCompileRequest& frontend = artifact.GetSourcePlan().GetRequest();
			const Span<const ShaderSourceSnapshot> sources = artifact.GetSourcePlan().GetSources();
			if (aggregateSources > input.Limits.MaximumSources || sources.Size() > input.Limits.MaximumSources - aggregateSources)
			{
				return invalid();
			}

			aggregateSources += sources.Size();

			for (const ShaderSourceSnapshot& source : sources)
			{
				if (source.Source.Size() > input.Limits.MaximumSourceBytes ||
					aggregateSourceBytes > input.Limits.MaximumAggregateSourceBytes ||
					source.Source.Size() > input.Limits.MaximumAggregateSourceBytes - aggregateSourceBytes)
				{
					return invalid();
				}

				aggregateSourceBytes += source.Source.Size();
			}

			if (frontend.Language != request.Language ||
				frontend.Optimization != request.Optimization ||
				frontend.DebugInfo != request.DebugInfo ||
				frontend.MatrixOrder != request.MatrixOrder ||
				frontend.bWarningsAsErrors != request.bWarningsAsErrors ||
				frontend.bPreciseFloatingPoint != request.bPreciseFloatingPoint)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Module frontend options differ from the exact link options."));
			}

			for (const ShaderTargetProfile& target : input.Targets)
			{
				if (Find(frontend.Targets.begin(), frontend.Targets.end(), target) == frontend.Targets.end())
				{
					return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Requested link target was not captured by this module profile."));
				}
			}

			const Span<const ShaderModuleRecord> records = artifact.GetModules();
			if (aggregateModules > input.Limits.MaximumModules || records.Size() > input.Limits.MaximumModules - aggregateModules)
			{
				return invalid();
			}

			aggregateModules += records.Size();

			for (uint32 row = 0; row < records.Size(); ++row)
			{
				const ShaderModuleRecord& record = records[row];
				if (record.ByteCount > input.Limits.MaximumModuleBytes ||
					aggregateBytes > input.Limits.MaximumAggregateModuleBytes ||
					record.ByteCount > input.Limits.MaximumAggregateModuleBytes - aggregateBytes)
				{
					return invalid();
				}

				aggregateBytes += record.ByteCount;

				for (usize previous = 0; previous <= index; ++previous)
				{
					const Span<const ShaderModuleRecord> priorRecords = modules[previous].GetModules();
					const uint32 priorCount = previous == index ? row : static_cast<uint32>(priorRecords.Size());

					for (uint32 priorRow = 0; priorRow < priorCount; ++priorRow)
					{
						const ShaderModuleRecord& prior = priorRecords[priorRow];
						if (prior.Name != record.Name)
						{
							continue;
						}

						if (artifact.SharesStorageWith(modules[previous]) && row == priorRow)
						{
							continue;
						}

						const Span<const Byte> bytes = artifact.GetModuleBytes(row);
						const Span<const Byte> priorBytes = modules[previous].GetModuleBytes(priorRow);
						if (prior.SourceIdentity != record.SourceIdentity ||
							prior.IRHash != record.IRHash ||
							bytes.Size() != priorBytes.Size() ||
							CompareBytes(bytes.Data(), priorBytes.Data(), bytes.Size()) != 0)
						{
							return R::Failure(MakeBuildError(EErrorCode::Conflict, "One logical module namespace has conflicting captured IR."));
						}
					}
				}
			}

			for (const ShaderSourceSnapshot& source : artifact.GetSourcePlan().GetSources())
			{
				if (cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				for (usize previous = 0; previous < index; ++previous)
				{
					for (const ShaderSourceSnapshot& prior : modules[previous].GetSourcePlan().GetSources())
					{
						if (prior.Identity == source.Identity && (prior.ContentHash != source.ContentHash || prior.Source != source.Source))
						{
							return R::Failure(MakeBuildError(EErrorCode::Conflict, "Module source closures disagree on one canonical identity."));
						}
					}
				}
			}

			storage.Modules.AddRequired(artifact);

			for (const ShaderDependency& dependency : artifact.GetDependencies())
			{
				const auto found = FindIf(
					storage.Dependencies.begin(), storage.Dependencies.end(), [&](const ShaderDependency& existing) { return existing.Identity == dependency.Identity; });
				if (found != storage.Dependencies.end())
				{
					continue;
				}

				ShaderDependency copied(resource);
				copied.ContentHash = dependency.ContentHash;
				if (!copied.Identity.Assign(dependency.Identity) || !storage.Dependencies.Add(Move(copied)))
				{
					return R::Failure(MakeBuildAllocationError());
				}
			}
		}

		for (const ShaderLinkEntrySelection& selected : input.EntryPoints)
		{
			if (selected.ModuleIndex >= modules.Size() || selected.Name.IsEmpty() || selected.Name.Size() > 4096 || selected.Name.View().Contains('\0'))
			{
				return invalid();
			}

			const ShaderModuleArtifact& artifact = modules[selected.ModuleIndex];
			const Span<const ShaderModuleEntryRecord> entries = artifact.GetEntryPoints();
			const auto found = FindIf(
				entries.begin(), entries.end(), [&](const ShaderModuleEntryRecord& entry)
				{ return entry.ModuleIndex == artifact.GetRootModuleIndex() && entry.Name == selected.Name && entry.Stage == selected.Stage; });
			if (found == entries.end())
			{
				return invalid();
			}

			if (selected.Arguments.Size() > MAXIMUM_SHADER_LINK_ARGUMENTS || selected.Arguments.Size() != found->SpecializationParameters.Size())
			{
				return invalid();
			}

			if (!selected.Arguments.IsEmpty() && selected.Stage != shader::EShaderStage::Compute)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Generic entry specialization requires the compute profile."));
			}

			for (const ShaderLinkEntrySelection& prior : storage.LinkRequest.EntryPoints)
			{
				if (prior.Name == selected.Name)
				{
					return R::Failure(MakeBuildError(EErrorCode::Conflict, "Final executable entry names must be unique."));
				}
			}

			ShaderLinkEntrySelection copied(resource);
			copied.ModuleIndex = selected.ModuleIndex;
			copied.Stage = selected.Stage;
			shader::ShaderEntryPoint entry(resource);
			entry.Stage = selected.Stage;
			if (!copied.Name.Assign(selected.Name) || !entry.Name.Assign(selected.Name))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			if (!copied.Arguments.Reserve(selected.Arguments.Size()))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			for (usize argumentIndex = 0; argumentIndex < selected.Arguments.Size(); ++argumentIndex)
			{
				const ShaderLinkArgument& argument = selected.Arguments[argumentIndex];
				if (argument.Kind != found->SpecializationParameters[argumentIndex].Kind)
				{
					return invalid();
				}

				if (argument.Kind == EShaderLinkArgumentKind::UInt32)
				{
					if (argument.TypeModuleIndex != 0 || !argument.TypeName.IsEmpty())
					{
						return invalid();
					}
				}
				else if (argument.Kind == EShaderLinkArgumentKind::Type)
				{
					if (argument.TypeModuleIndex >= modules.Size() || argument.UInt32Value != 0 || argument.TypeName.IsEmpty() || argument.TypeName.Size() > 256)
					{
						return invalid();
					}

					for (usize characterIndex = 0; characterIndex < argument.TypeName.Size(); ++characterIndex)
					{
						const char character = argument.TypeName[characterIndex];
						if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_' ||
								(characterIndex != 0 && character >= '0' && character <= '9')))
						{
							return invalid();
						}
					}
				}
				else
				{
					return invalid();
				}

				ShaderLinkArgument copiedArgument(resource);
				copiedArgument.Kind = argument.Kind;
				copiedArgument.TypeModuleIndex = argument.TypeModuleIndex;
				copiedArgument.UInt32Value = argument.UInt32Value;
				if (!copiedArgument.TypeName.Assign(argument.TypeName))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				copied.Arguments.AddRequired(Move(copiedArgument));
			}

			storage.LinkRequest.EntryPoints.AddRequired(Move(copied));
			request.EntryPoints.AddRequired(Move(entry));
		}

		for (const ShaderTargetProfile& target : input.Targets)
		{
			if (Find(request.Targets.begin(), request.Targets.end(), target) != request.Targets.end())
			{
				return invalid();
			}

			request.Targets.AddRequired(target);
			storage.LinkRequest.Targets.AddRequired(target);
			const Span<const ShaderCompilerProfile> profiles = compiler.GetProfiles();
			const auto found = FindIf(
				profiles.begin(), profiles.end(), [&](const ShaderCompilerProfile& profile)
				{ return profile.Language == request.Language && profile.Target == target && profile.bLinking && profile.ShaderAbiVersion == storage.Toolchain.ShaderAbiVersion; });
			if (found == profiles.end())
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Module linking is unavailable for the exact target profile."));
			}

			if (!found->bGenericEntrySpecialization)
			{
				for (const ShaderLinkEntrySelection& selected : input.EntryPoints)
				{
					if (!selected.Arguments.IsEmpty())
					{
						return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Typed generic linking is unavailable for this profile."));
					}
				}
			}
		}

		Result<void> supported = ValidateProfiles(request, compiler.GetProfiles(), storage.Toolchain.ShaderAbiVersion);
		if (!supported)
		{
			return R::Failure(Move(supported.GetError()));
		}

		if (storage.Dependencies.Size() > 1)
		{
			Sort(
				storage.Dependencies.begin(), storage.Dependencies.end(),
				[](const ShaderDependency& a, const ShaderDependency& b) { return a.Identity.View() < b.Identity.View(); });
		}

		Sha256 identity;
		auto number = [&](uint64 value)
		{
			Byte bytes[8];

			for (unsigned index = 0; index < 8; ++index)
			{
				bytes[index] = static_cast<Byte>(value >> (index * 8));
			}

			identity.Update(bytes);
		};

		auto text = [&](StringView value)
		{
			number(value.Size());
			identity.Update({reinterpret_cast<const Byte*>(value.Data()), value.Size()});
		};

		text("ROX-SHADER-LINK-2");
		number(modules.Size());

		for (const ShaderModuleArtifact& module : modules)
		{
			const Hash256 hash = module.GetModuleIdentity();
			identity.Update({reinterpret_cast<const Byte*>(hash.Bytes.data()), hash.Bytes.size()});
		}

		number(input.EntryPoints.Size());

		for (const ShaderLinkEntrySelection& entry : input.EntryPoints)
		{
			number(entry.ModuleIndex);
			text(entry.Name);
			number(static_cast<uint32>(entry.Stage));
			number(entry.Arguments.Size());

			for (const ShaderLinkArgument& argument : entry.Arguments)
			{
				number(static_cast<uint32>(argument.Kind));
				number(argument.TypeModuleIndex);
				text(argument.TypeName);
				number(argument.UInt32Value);
			}
		}

		number(input.Targets.Size());

		for (const ShaderTargetProfile& target : input.Targets)
		{
			number(static_cast<uint32>(target.Target));
			number(target.ShaderModelMajor);
			number(target.ShaderModelMinor);
			number(target.SpirVMajor);
			number(target.SpirVMinor);
			number(target.EnvironmentMajor);
			number(target.EnvironmentMinor);
			number(target.ProfileVersion);
		}

		number(static_cast<uint32>(input.Optimization));
		number(static_cast<uint32>(input.DebugInfo));
		number(static_cast<uint32>(input.MatrixOrder));
		number(input.bWarningsAsErrors ? 1 : 0);
		number(input.bPreciseFloatingPoint ? 1 : 0);
		storage.Identity = identity.Finalize();
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		return R::Success(Move(plan));
	}


	Result<Array<Byte>> ShaderBuildPlan::EncodeInputs(MemoryResource& resource, uint64 maximumBytes, CancellationToken cancellation) const
	{
		using R = Result<Array<Byte>>;

		Result<void> validated = validateInputs(*this, maximumBytes, cancellation);

		if (!validated)
		{
			return R::Failure(Move(validated.GetError()));
		}

		ShaderBuildInputWriter counter(maximumBytes, cancellation);
		counter.U32(SHADER_BUILD_INPUT_FORMAT_VERSION);
		writeInputsRecursive(counter, 0);
		Result<void> counted = counter.Finish();

		if (!counted)
		{
			return R::Failure(Move(counted.GetError()));
		}

		Array<Byte> bytes(resource);

		if (!bytes.ResizeUninitialized(static_cast<usize>(counter.GetByteCount())))
		{
			return R::Failure(EErrorCode::ExternalFailure, "ROX-SHADER-INPUT-ALLOCATION", "Shader input allocation failed.");
		}

		ShaderBuildInputWriter writer(bytes.View(), maximumBytes, cancellation);
		writer.U32(SHADER_BUILD_INPUT_FORMAT_VERSION);
		writeInputsRecursive(writer, 0);
		Result<void> written = writer.Finish();

		if (!written)
		{
			return R::Failure(Move(written.GetError()));
		}

		ASSERT(writer.GetByteCount() == counter.GetByteCount(), "Immutable shader input counts must remain stable.");
		return R::Success(Move(bytes));
	}

	Result<void> ShaderBuildPlan::VerifyInputs(Span<const Byte> bytes, uint64 maximumBytes, CancellationToken cancellation) const
	{
		Result<void> validated = validateInputs(*this, maximumBytes, cancellation);

		if (!validated)
		{
			return validated;
		}

		if (bytes.Size() > maximumBytes)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "ROX-SHADER-INPUT-LIMIT", "Input record exceeds its byte bound.");
		}

		if (bytes.Size() < 4)
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "ROX-SHADER-INPUT-TRUNCATED", "Input record has no complete version.");
		}

		Byte versionBytes[4];
		ByteWriter versionWriter(versionBytes);
		versionWriter.WriteU32LE(SHADER_BUILD_INPUT_FORMAT_VERSION);

		if (CompareBytes(bytes.Data(), versionBytes, 4) != 0)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "ROX-SHADER-INPUT-VERSION", "Input record version is unsupported.");
		}

		ShaderBuildInputWriter writer(bytes, maximumBytes, cancellation);
		writer.U32(SHADER_BUILD_INPUT_FORMAT_VERSION);
		writeInputsRecursive(writer, 0);

		return writer.Finish();
	}
} // namespace rox::shader
