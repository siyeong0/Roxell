#include "DeviceState.h"
#include "Vulkan/SpirvDescriptorRequirements.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<ShaderHandle> Device::CreateShader(const ShaderDesc& desc)
	{
		CallScope call(*this, false);

		ASSERT(!(desc.Stage > EShaderStage::Callable ||
				desc.Bytecode.IsEmpty() ||
				desc.Bytecode.Size() > 16 * 1024 * 1024 ||
				desc.EntryPoint == nullptr ||
				CStringLength(desc.EntryPoint) == 0 ||
				CStringLength(desc.EntryPoint) > 63),
			"The shader stage, bytecode, or entry point is invalid.");

		if (m_State->m_Kind == EBackend::Vulkan)
		{
			Result<void> supported = ValidateSpirvDescriptorIndexing(desc.Bytecode, m_State->m_Backend.Capabilities);
			if (!supported)
			{
				return Result<ShaderHandle>::Failure(Move(supported.GetError()));
			}
		}

		const bool bNewSlot = m_State->m_FreeShader == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Shaders.Size()) : m_State->m_FreeShader;

		if (bNewSlot && (m_State->m_Shaders.Size() >= NumericLimits<uint32>::max() || !m_State->m_Shaders.Add(ShaderSlot(m_State->m_Resource))))
		{
			return Result<ShaderHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a shader slot.");
		}
		ShaderSlot& slot = m_State->m_Shaders[index];
		slot.Bytecode.Clear();

		if (!slot.Bytecode.ResizeUninitialized(desc.Bytecode.Size()))
		{
			if (bNewSlot)
			{
				slot.NextFree = m_State->m_FreeShader;
				m_State->m_FreeShader = index;
			}

			return Result<ShaderHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not copy shader bytecode.");
		}

		CopyBytes(slot.Bytecode.Data(), desc.Bytecode.Data(), desc.Bytecode.Size());

		if (!bNewSlot)
		{
			m_State->m_FreeShader = slot.NextFree;
		}
		slot.Stage = desc.Stage;
		slot.Library = {};
		slot.LibraryEntry = 0;
		CopyBytes(slot.EntryPoint, desc.EntryPoint, CStringLength(desc.EntryPoint) + 1);
		slot.NextFree = NumericLimits<uint32>::max();
		slot.bLive = true;

		return Result<ShaderHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyShader(ShaderHandle handle)
	{
		CallScope call(*this, false);
		ShaderSlot* const slot = findShader(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->Library.IsValid())
		{
			--m_State->m_ShaderLibraries[slot->Library.Slot].References;
		}
		else
		{
			slot->Bytecode.Reset();
		}
		slot->Library = {};
		slot->LibraryEntry = 0;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeShader;
			m_State->m_FreeShader = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] ShaderSlot* Device::findShader(ShaderHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Shaders.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		ShaderSlot& slot = m_State->m_Shaders[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] Result<void> Device::resolveShader(ShaderHandle handle, EShaderStage stage, Span<const Byte>& bytecode, const char*& entryPoint) noexcept
	{
		if (handle == ShaderHandle{})
		{
			return Result<void>::Success();
		}
		ShaderSlot* const slot = findShader(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->Stage == stage && bytecode.IsEmpty() && entryPoint != nullptr && CompareCString(entryPoint, "main") == 0,
			"Shader handles match the pipeline stage and have no competing source.");

		const Array<Byte>& source = slot->Library.IsValid() ? m_State->m_ShaderLibraries[slot->Library.Slot].Entries[slot->LibraryEntry].Bytecode : slot->Bytecode;
		bytecode = { source.Data(), source.Size() };
		entryPoint = slot->EntryPoint;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::resolveGraphicsShaders(NativeGraphicsPipelineDesc& desc) noexcept
	{
		const auto applyStage = [](const ShaderStageDesc& stage, ShaderHandle& handle,
			Span<const Byte>& code, const char*& entryPoint,
			Span<const ShaderSpecialization>& specializations)
			{
				ASSERT(stage.Source.GetKind() != ShaderInput::EKind::None || stage.Specializations.IsEmpty(), "Shader specialization requires a shader source.");
				if (!stage.Specializations.IsEmpty())
				{
					specializations = stage.Specializations;
				}
				if (stage.Source.GetKind() == ShaderInput::EKind::Code)
				{
					ASSERT(!stage.Source.GetCode().IsEmpty() && stage.Source.GetEntryPoint() != nullptr, "A shader code source requires bytecode and an entry point.");
					handle = {};
					code = stage.Source.GetCode();
					entryPoint = stage.Source.GetEntryPoint();
					specializations = stage.Specializations;
				}
				else if (stage.Source.GetKind() == ShaderInput::EKind::Shader)
				{
					ASSERT(stage.Source.GetShader().IsValid(), "A shader source handle must be valid.");
					handle = stage.Source.GetShader();
					code = {};
					entryPoint = "main";
					specializations = stage.Specializations;
				}
			};
		applyStage(desc.Vertex, desc.VertexShader, desc.VertexShaderBytecode, desc.VertexEntryPoint, desc.VertexSpecializations);
		applyStage(desc.Pixel, desc.FragmentShader, desc.FragmentShaderBytecode, desc.FragmentEntryPoint, desc.FragmentSpecializations);
		applyStage(desc.Geometry, desc.GeometryShader, desc.GeometryShaderBytecode, desc.GeometryEntryPoint, desc.GeometrySpecializations);
		applyStage(desc.Hull, desc.HullShader, desc.HullShaderBytecode, desc.HullEntryPoint, desc.HullSpecializations);
		applyStage(desc.Domain, desc.DomainShader, desc.DomainShaderBytecode, desc.DomainEntryPoint, desc.DomainSpecializations);
		applyStage(desc.Mesh, desc.MeshShader, desc.MeshShaderBytecode, desc.MeshEntryPoint, desc.MeshSpecializations);
		applyStage(desc.Task, desc.TaskShader, desc.TaskShaderBytecode, desc.TaskEntryPoint, desc.TaskSpecializations);

		Result<void> vertex = resolveShader(desc.VertexShader, EShaderStage::Vertex, desc.VertexShaderBytecode, desc.VertexEntryPoint);

		if (!vertex)
		{
			return vertex;
		}

		Result<void> fragment = resolveShader(desc.FragmentShader, EShaderStage::Pixel, desc.FragmentShaderBytecode, desc.FragmentEntryPoint);

		if (!fragment)
		{
			return fragment;
		}

		Result<void> geometry = resolveShader(desc.GeometryShader, EShaderStage::Geometry, desc.GeometryShaderBytecode, desc.GeometryEntryPoint);

		if (!geometry)
		{
			return geometry;
		}

		Result<void> hull = resolveShader(desc.HullShader, EShaderStage::Hull, desc.HullShaderBytecode, desc.HullEntryPoint);

		if (!hull)
		{
			return hull;
		}

		Result<void> domain = resolveShader(desc.DomainShader, EShaderStage::Domain, desc.DomainShaderBytecode, desc.DomainEntryPoint);

		if (!domain)
		{
			return domain;
		}

		Result<void> mesh = resolveShader(desc.MeshShader, EShaderStage::Mesh, desc.MeshShaderBytecode, desc.MeshEntryPoint);

		if (!mesh)
		{
			return mesh;
		}

		return resolveShader(desc.TaskShader, EShaderStage::Task, desc.TaskShaderBytecode, desc.TaskEntryPoint);
	}

	[[nodiscard]] Result<ShaderLibraryHandle> Device::CreateShaderLibrary(Span<const ShaderLibraryEntryDesc> entries)
	{
		CallScope call(*this, false);

		ASSERT(!(entries.IsEmpty() || entries.Size() > 64), "The shader library entry count is invalid.");

		usize totalBytes = 0;

		for (usize index = 0; index < entries.Size(); ++index)
		{
			const ShaderLibraryEntryDesc& entry = entries[index];

			ASSERT(!(entry.Stage > EShaderStage::Callable ||
					entry.Bytecode.IsEmpty() ||
					entry.Bytecode.Size() > 16 * 1024 * 1024 ||
					entry.EntryPoint == nullptr ||
					CStringLength(entry.EntryPoint) == 0 ||
					CStringLength(entry.EntryPoint) > 63 ||
					entry.Bytecode.Size() > 64 * 1024 * 1024 - totalBytes),
				"A shader library entry is invalid.");
			if (m_State->m_Kind == EBackend::Vulkan)
			{
				Result<void> supported = ValidateSpirvDescriptorIndexing(entry.Bytecode, m_State->m_Backend.Capabilities);
				if (!supported)
				{
					return Result<ShaderLibraryHandle>::Failure(Move(supported.GetError()));
				}
			}

			totalBytes += entry.Bytecode.Size();

			for (usize previous = 0; previous < index; ++previous)
			{
				ASSERT(!(entries[previous].Stage == entry.Stage && CompareCString(entries[previous].EntryPoint, entry.EntryPoint) == 0), "A shader stage and entry point are duplicated.");
			}
		}

		const bool bNewSlot = m_State->m_FreeShaderLibrary == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_ShaderLibraries.Size()) : m_State->m_FreeShaderLibrary;

		if (bNewSlot && (m_State->m_ShaderLibraries.Size() >= NumericLimits<uint32>::max() || !m_State->m_ShaderLibraries.Add(ShaderLibrarySlot(m_State->m_Resource))))
		{
			return Result<ShaderLibraryHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a shader library slot.");
		}
		ShaderLibrarySlot& slot = m_State->m_ShaderLibraries[index];
		slot.Entries.Clear();
		auto allocationFailure = [&]() -> Result<ShaderLibraryHandle>
			{
				slot.Entries.Clear();

				if (bNewSlot)
				{
					slot.NextFree = m_State->m_FreeShaderLibrary;
					m_State->m_FreeShaderLibrary = index;
				}

				return Result<ShaderLibraryHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not copy shader library bytecode.");
			};
		if (!slot.Entries.Reserve(entries.Size()))
		{
			return allocationFailure();
		}

		for (const ShaderLibraryEntryDesc& source : entries)
		{
			if (!slot.Entries.Add(ShaderLibraryEntry(m_State->m_Resource)))
			{
				return allocationFailure();
			}
			ShaderLibraryEntry& destination = slot.Entries.Back();

			if (!destination.Bytecode.ResizeUninitialized(source.Bytecode.Size()))
			{
				return allocationFailure();
			}

			CopyBytes(destination.Bytecode.Data(), source.Bytecode.Data(), source.Bytecode.Size());
			CopyBytes(destination.EntryPoint, source.EntryPoint, CStringLength(source.EntryPoint) + 1);
			destination.Stage = source.Stage;
		}

		if (!bNewSlot)
		{
			m_State->m_FreeShaderLibrary = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.References = 0;
		slot.bLive = true;

		return Result<ShaderLibraryHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<ShaderHandle> Device::AcquireShader(ShaderLibraryHandle library, EShaderStage stage, const char* entryPoint)
	{
		CallScope call(*this, false);
		ShaderLibrarySlot* const source = findShaderLibrary(library);

		ASSERT(source != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(stage > EShaderStage::Callable || entryPoint == nullptr || CStringLength(entryPoint) == 0 || CStringLength(entryPoint) > 63),
			"The shader entry query is invalid.");

		uint32 entryIndex = 0;

		for (; entryIndex < source->Entries.Size(); ++entryIndex)
		{
			if (source->Entries[entryIndex].Stage == stage && CompareCString(source->Entries[entryIndex].EntryPoint, entryPoint) == 0)
			{
				break;
			}
		}

		if (entryIndex == source->Entries.Size())
		{
			return Result<ShaderHandle>::Failure(EErrorCode::NotFound, "Rhi.ShaderLibrary", "The shader entry was not found.");
		}

		if (source->References == NumericLimits<uint32>::max())
		{
			return Result<ShaderHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.ShaderLibrary", "The library has too many shader references.");
		}

		const bool bNewSlot = m_State->m_FreeShader == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Shaders.Size()) : m_State->m_FreeShader;

		if (bNewSlot && (m_State->m_Shaders.Size() >= NumericLimits<uint32>::max() || !m_State->m_Shaders.Add(ShaderSlot(m_State->m_Resource))))
		{
			return Result<ShaderHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a shader slot.");
		}
		ShaderSlot& slot = m_State->m_Shaders[index];

		if (!bNewSlot)
		{
			m_State->m_FreeShader = slot.NextFree;
		}
		slot.Bytecode.Reset();
		slot.Stage = stage;
		slot.Library = library;
		slot.LibraryEntry = entryIndex;
		CopyBytes(slot.EntryPoint, entryPoint, CStringLength(entryPoint) + 1);
		slot.NextFree = NumericLimits<uint32>::max();
		slot.bLive = true;
		++source->References;

		return Result<ShaderHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyShaderLibrary(ShaderLibraryHandle handle)
	{
		CallScope call(*this, false);
		ShaderLibrarySlot* const slot = findShaderLibrary(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->References != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.ShaderLibrary", "A shader handle still references the library.");
		}
		slot->Entries.Reset();
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeShaderLibrary;
			m_State->m_FreeShaderLibrary = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] ShaderLibrarySlot* Device::findShaderLibrary(ShaderLibraryHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_ShaderLibraries.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		ShaderLibrarySlot& slot = m_State->m_ShaderLibraries[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}
}
