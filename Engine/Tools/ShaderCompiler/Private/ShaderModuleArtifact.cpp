#include <ShaderModuleArtifact.h>

#include "ShaderCompilerInternal.h"

#include <Assertion.h>
#include <ByteOperations.hpp>
#include <NumericLimits.hpp>
#include <Sha256.h>
#include <TextPrimitives.hpp>
#include <Types.h>

namespace rox::shader
{
	using namespace core;

	class ShaderModuleStorage final
	{
	public:
		explicit ShaderModuleStorage(MemoryResource& resource) noexcept
			: m_Modules(resource)
			, m_Bytes(resource)
			, m_Entries(resource)
		{
		}

		ShaderBuildPlan m_SourcePlan;
		Array<ShaderModuleRecord> m_Modules;
		Array<Array<Byte>> m_Bytes;
		Array<ShaderModuleEntryRecord> m_Entries;
		Hash256 m_Identity;
		uint32 m_RootModule = 0;
	};

	Result<ShaderModuleArtifact> ShaderModuleArtifact::Create(
		const ShaderBuildPlan& sourcePlan,
		StringView moduleName,
		uint32 rootModule,
		Array<ShaderModuleRecord>&& modules,
		Array<Array<Byte>>&& moduleBytes,
		Array<ShaderModuleEntryRecord>&& entries,
		MemoryResource& resource)
	{
		using R = Result<ShaderModuleArtifact>;

		if (!sourcePlan.IsValid() ||
			sourcePlan.GetKind() != EShaderBuildPlanKind::SourceModule ||
			moduleName.IsEmpty() ||
			moduleName.Size() > 256 ||
			moduleName.Contains('\0') ||
			modules.IsEmpty() ||
			modules.Size() != moduleBytes.Size() ||
			rootModule >= modules.Size())
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module publication requires a source module plan and complete records."));
		}

		if (&modules.GetResource() != &resource || &moduleBytes.GetResource() != &resource || &entries.GetResource() != &resource)
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module publication requires arrays owned by its memory resource."));
		}

		const ShaderBuildLimits& limits = sourcePlan.GetRequest().Limits;
		if (modules.Size() > limits.MaximumModules || entries.Size() > 256)
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module publication exceeds its record limits."));
		}

		uint64 aggregateBytes = 0;

		for (usize index = 0; index < modules.Size(); ++index)
		{
			const ShaderModuleRecord& record = modules[index];
			const uint64 byteCount = moduleBytes[index].Size();
			if (&moduleBytes[index].GetResource() != &resource ||
				&record.Name.GetResource() != &resource ||
				&record.SourceIdentity.GetResource() != &resource ||
				record.Name.IsEmpty() ||
				record.Name.Size() > 4096 ||
				record.Name.View().Contains('\0') ||
				!sourcePlan.FindSource(record.SourceIdentity) ||
				byteCount == 0 ||
				record.ByteCount != byteCount ||
				byteCount > limits.MaximumModuleBytes ||
				aggregateBytes > limits.MaximumAggregateModuleBytes ||
				byteCount > limits.MaximumAggregateModuleBytes - aggregateBytes)
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module record is outside its captured source or byte bounds."));
			}

			aggregateBytes += byteCount;
		}

		for (const ShaderModuleEntryRecord& entry : entries)
		{
			if (&entry.Name.GetResource() != &resource ||
				&entry.SpecializationParameters.GetResource() != &resource ||
				entry.SpecializationParameters.Size() > MAXIMUM_SHADER_LINK_ARGUMENTS ||
				entry.ModuleIndex >= modules.Size() ||
				entry.Name.IsEmpty() ||
				entry.Name.Size() > 4096 ||
				entry.Name.View().Contains('\0') ||
				(entry.Stage != EShaderStage::Compute && entry.Stage != EShaderStage::Vertex && entry.Stage != EShaderStage::Pixel))
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module entry requires a valid module, name and stage."));
			}

			for (usize index = 0; index < entry.SpecializationParameters.Size(); ++index)
			{
				const ShaderModuleSpecializationParameter& parameter = entry.SpecializationParameters[index];
				if (&parameter.Name.GetResource() != &resource ||
					parameter.Name.IsEmpty() ||
					parameter.Name.Size() > 4096 ||
					parameter.Name.View().Contains('\0') ||
					(parameter.Kind != EShaderLinkArgumentKind::Type && parameter.Kind != EShaderLinkArgumentKind::UInt32))
				{
					return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module specialization requires an owned name and supported parameter kind."));
				}

				for (usize previous = 0; previous < index; ++previous)
				{
					if (entry.SpecializationParameters[previous].Name == parameter.Name)
					{
						return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module specialization parameter names must be unique."));
					}
				}
			}
		}

		Sha256 identity;
		auto hashU64 = [&](uint64 value)
		{
			Byte bytes[8];

			for (unsigned index = 0; index < 8; ++index)
			{
				bytes[index] = static_cast<Byte>(value >> (index * 8));
			}

			identity.Update(bytes);
		};

		auto hashText = [&](StringView value)
		{
			hashU64(value.Size());
			identity.Update({reinterpret_cast<const Byte*>(value.Data()), value.Size()});
		};

		hashText("ROX-SHADER-MODULE-2");
		hashText(moduleName);
		const Hash256 sourceIdentity = sourcePlan.GetBuildIdentity();
		identity.Update({reinterpret_cast<const Byte*>(sourceIdentity.Bytes.data()), sourceIdentity.Bytes.size()});
		hashU64(rootModule);
		hashU64(modules.Size());

		for (uint32 index = 0; index < modules.Size(); ++index)
		{
			const ShaderModuleRecord& record = modules[index];
			hashText(record.Name);
			hashText(record.SourceIdentity);
			hashU64(record.ByteCount);
			identity.Update(moduleBytes[index].View());
		}

		hashU64(entries.Size());

		for (const ShaderModuleEntryRecord& entry : entries)
		{
			hashU64(entry.ModuleIndex);
			hashText(entry.Name);
			hashU64(static_cast<uint32>(entry.Stage));
			hashU64(entry.SpecializationParameters.Size());

			for (const ShaderModuleSpecializationParameter& parameter : entry.SpecializationParameters)
			{
				hashU64(static_cast<uint32>(parameter.Kind));
				hashText(parameter.Name);
			}
		}

		ShaderModuleArtifact output;
		if (!MakeSharedInto(resource, &output.m_Storage, resource))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		ShaderModuleStorage& storage = *output.m_Storage;
		storage.m_SourcePlan = sourcePlan;
		storage.m_RootModule = rootModule;
		storage.m_Identity = identity.Finalize();
		storage.m_Modules = Move(modules);
		storage.m_Bytes = Move(moduleBytes);
		storage.m_Entries = Move(entries);

		return R::Success(Move(output));
	}

	ShaderModuleArtifact::ShaderModuleArtifact() noexcept = default;
	ShaderModuleArtifact::~ShaderModuleArtifact() = default;
	ShaderModuleArtifact::ShaderModuleArtifact(const ShaderModuleArtifact&) noexcept = default;
	ShaderModuleArtifact& ShaderModuleArtifact::operator=(const ShaderModuleArtifact&) noexcept = default;
	ShaderModuleArtifact::ShaderModuleArtifact(ShaderModuleArtifact&&) noexcept = default;
	ShaderModuleArtifact& ShaderModuleArtifact::operator=(ShaderModuleArtifact&&) noexcept = default;

	bool ShaderModuleArtifact::IsValid() const noexcept
	{
		return static_cast<bool>(m_Storage);
	}

	Hash256 ShaderModuleArtifact::GetModuleIdentity() const noexcept
	{
		return m_Storage ? m_Storage->m_Identity : Hash256{};
	}

	const ShaderToolchainIdentity& ShaderModuleArtifact::GetToolchainIdentity() const noexcept
	{
		ASSERT(IsValid(), "Toolchain inspection requires a valid module artifact.");
		return m_Storage->m_SourcePlan.GetToolchainIdentity();
	}

	uint32 ShaderModuleArtifact::GetRootModuleIndex() const noexcept
	{
		ASSERT(IsValid(), "Root selection requires a valid module artifact.");
		return m_Storage->m_RootModule;
	}

	Span<const ShaderModuleRecord> ShaderModuleArtifact::GetModules() const noexcept
	{
		return m_Storage ? m_Storage->m_Modules.View() : Span<const ShaderModuleRecord>{};
	}

	Span<const Byte> ShaderModuleArtifact::GetModuleBytes(uint32 index) const noexcept
	{
		ASSERT(IsValid() && index < m_Storage->m_Bytes.Size(), "Module byte inspection requires a valid bundle index.");
		return m_Storage->m_Bytes[index].View();
	}

	Span<const ShaderModuleEntryRecord> ShaderModuleArtifact::GetEntryPoints() const noexcept
	{
		return m_Storage ? m_Storage->m_Entries.View() : Span<const ShaderModuleEntryRecord>{};
	}

	Span<const ShaderDependency> ShaderModuleArtifact::GetDependencies() const noexcept
	{
		return m_Storage ? m_Storage->m_SourcePlan.GetDependencies() : Span<const ShaderDependency>{};
	}

	const ShaderBuildPlan& ShaderModuleArtifact::GetSourcePlan() const noexcept
	{
		ASSERT(IsValid(), "Source authority requires a live module artifact.");
		return m_Storage->m_SourcePlan;
	}

	bool ShaderModuleArtifact::SharesStorageWith(const ShaderModuleArtifact& other) const noexcept
	{
		return m_Storage.Get() == other.m_Storage.Get();
	}

	bool ShaderModuleArtifact::CanShareAcrossThreads() const noexcept
	{
		return IsValid() && m_Storage.GetResource()->GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe && m_Storage->m_SourcePlan.CanShareAcrossThreads();
	}

	uint64 ShaderModuleArtifact::GetRetainedByteCount() const noexcept
	{
		ASSERT(IsValid(), "Retained module accounting requires a valid artifact.");
		const uint64 sourceCharge = m_Storage->m_SourcePlan.GetRetainedByteCount();
		if (sourceCharge > NumericLimits<uint64>::max() - sizeof(ShaderModuleStorage))
		{
			return NumericLimits<uint64>::max();
		}

		uint64 bytes = sizeof(ShaderModuleStorage) + sourceCharge;
		auto chargeString = [&](const String& text)
		{
			bytes += text.Capacity() + 1;
		};

		bytes += m_Storage->m_Modules.Capacity() * sizeof(ShaderModuleRecord);
		bytes += m_Storage->m_Bytes.Capacity() * sizeof(Array<Byte>);
		bytes += m_Storage->m_Entries.Capacity() * sizeof(ShaderModuleEntryRecord);

		for (const ShaderModuleRecord& record : m_Storage->m_Modules)
		{
			chargeString(record.Name);
			chargeString(record.SourceIdentity);
		}

		for (const ShaderModuleEntryRecord& record : m_Storage->m_Entries)
		{
			chargeString(record.Name);
			bytes += record.SpecializationParameters.Capacity() * sizeof(ShaderModuleSpecializationParameter);

			for (const ShaderModuleSpecializationParameter& parameter : record.SpecializationParameters)
			{
				chargeString(parameter.Name);
			}
		}

		for (const Array<Byte>& data : m_Storage->m_Bytes)
		{
			bytes += data.Capacity();
		}

		return bytes;
	}

	bool ShaderModuleArtifact::HasSameInputs(const ShaderModuleArtifact& other) const noexcept
	{
		ASSERT(IsValid() && other.IsValid(), "Module input comparison requires live artifacts.");
		const ShaderModuleStorage& x = *m_Storage;
		const ShaderModuleStorage& y = *other.m_Storage;
		if (&x == &y)
		{
			return true;
		}

		if (x.m_RootModule != y.m_RootModule ||
			x.m_Modules.Size() != y.m_Modules.Size() ||
			x.m_Entries.Size() != y.m_Entries.Size() ||
			!x.m_SourcePlan.HasSameInputs(y.m_SourcePlan))
		{
			return false;
		}

		for (usize row = 0; row < x.m_Modules.Size(); ++row)
		{
			if (x.m_Modules[row].Name != y.m_Modules[row].Name ||
				x.m_Modules[row].SourceIdentity != y.m_Modules[row].SourceIdentity ||
				x.m_Bytes[row].Size() != y.m_Bytes[row].Size() ||
				CompareBytes(x.m_Bytes[row].Data(), y.m_Bytes[row].Data(), x.m_Bytes[row].Size()) != 0)
			{
				return false;
			}
		}

		for (usize row = 0; row < x.m_Entries.Size(); ++row)
		{
			if (x.m_Entries[row].ModuleIndex != y.m_Entries[row].ModuleIndex ||
				x.m_Entries[row].Name != y.m_Entries[row].Name ||
				x.m_Entries[row].Stage != y.m_Entries[row].Stage ||
				x.m_Entries[row].SpecializationParameters.Size() != y.m_Entries[row].SpecializationParameters.Size())
			{
				return false;
			}

			for (usize parameterIndex = 0; parameterIndex < x.m_Entries[row].SpecializationParameters.Size(); ++parameterIndex)
			{
				const ShaderModuleSpecializationParameter& leftParameter = x.m_Entries[row].SpecializationParameters[parameterIndex];
				const ShaderModuleSpecializationParameter& rightParameter = y.m_Entries[row].SpecializationParameters[parameterIndex];
				if (leftParameter.Kind != rightParameter.Kind || leftParameter.Name != rightParameter.Name)
				{
					return false;
				}
			}
		}

		return true;
	}
} // namespace rox::shader
