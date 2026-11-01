#include "PreparedBindingCache.h"
#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<SharedPtr<BoundDescriptorTables>> Device::captureDescriptorHeapBindings(Span<const DescriptorHeapBinding> inputs, bool bEnabled)
	{
		using R = Result<SharedPtr<BoundDescriptorTables>>;
		const auto fail = [](EErrorCode code)
			{
				Error error;
				error.Code = code;

				return R::Failure(Move(error));
			};
		if (inputs.IsEmpty())
		{
			return R::Success({});
		}
		if (!bEnabled || !m_State->m_Backend.Capabilities.Features.bDeviceHeapIndexing)
		{
			return fail(EErrorCode::Unsupported);
		}
		ASSERT(inputs.Size() <= 32, "RHI binding arguments must match their declared resources and ranges.");

		uint64 capacity = 0;
		for (usize index = 0; index < inputs.Size(); ++index)
		{
			const rhi::DescriptorHeapBinding& input = inputs[index];
			const DescriptorTableSlot* table = findDescriptorTable(input.Table);
			ASSERT(table != nullptr && input.UsedEntries.Size() <= table->Descriptor.Capacity, "RHI binding arguments must match their declared resources and ranges.");
			if (backend::HAS_GET_DESCRIPTOR_TABLE_DEVICE_HEAP_INDEX == false || backend::GetDescriptorTableDeviceHeapIndex(m_State->m_Backend.State, table->Native) == UINT32_MAX)
			{
				return fail(EErrorCode::Unsupported);
			}
			for (usize previous = 0; previous < index; ++previous)
			{
				ASSERT(inputs[previous].Table != input.Table, "RHI binding arguments must match their declared resources and ranges.");
			}
			capacity += input.UsedEntries.IsEmpty() ? table->Descriptor.Capacity : input.UsedEntries.Size();
		}
		ASSERT(capacity <= UINT32_MAX, "RHI binding arguments must match their declared resources and ranges.");

		SharedPtr<BoundDescriptorTables> owner;
		if (!MakeSharedInto(m_State->m_Resource, &owner, m_State->m_Resource) || !owner->Ranges.Reserve(inputs.Size()) || !owner->UsedEntries.Reserve(static_cast<usize>(capacity)))
		{
			return fail(EErrorCode::ExternalFailure);
		}

		for (const rhi::DescriptorHeapBinding& input : inputs)
		{
			rhi::DescriptorTableSlot const& table = m_State->m_DescriptorTables[input.Table.Slot];
			BoundDescriptorTableRange row;
			row.Table = input.Table;
			row.UsedOffset = static_cast<uint32>(owner->UsedEntries.Size());
			const uint32 count = input.UsedEntries.IsEmpty() ? table.Descriptor.Capacity : static_cast<uint32>(input.UsedEntries.Size());
			for (uint32 index = 0; index < count; ++index)
			{
				const uint32 used = input.UsedEntries.IsEmpty() ? index : input.UsedEntries[index];
				ASSERT(used < table.Descriptor.Capacity, "RHI binding arguments must match their declared resources and ranges.");
				rhi::DescriptorTableEntry const& entry = table.Entries[used];
				const bool bOccupied = entry.Buffer.IsValid() || entry.Texture.IsValid() || entry.Sampler.IsValid() || entry.AccelerationStructure.IsValid();
				if (!bOccupied)
				{
					ASSERT(input.UsedEntries.IsEmpty(), "RHI binding arguments must match their declared resources and ranges.");
					continue;
				}
				owner->UsedEntries.AddRequired(used);
			}

			row.UsedCount = static_cast<uint32>(owner->UsedEntries.Size()) - row.UsedOffset;
			if (!input.UsedEntries.IsEmpty() && row.UsedCount > 1)
			{
				uint32* const first = owner->UsedEntries.Data() + row.UsedOffset;
				Sort(first, first + row.UsedCount);
				for (uint32 index = 1; index < row.UsedCount; ++index)
				{
					ASSERT(first[index - 1] != first[index], "RHI binding arguments must match their declared resources and ranges.");
				}
			}
			owner->Ranges.AddRequired(row);
		}

		return R::Success(Move(owner));
	}

	void Device::fillNativeDeviceHeaps(const BoundDescriptorTables* owner, NativeDescriptorHeapBinding(&native)[32]) noexcept
	{
		if (owner == nullptr)
		{
			return;
		}
		for (usize index = 0; index < owner->Ranges.Size(); ++index)
		{
			rhi::BoundDescriptorTableRange const& row = owner->Ranges[index];
			rhi::DescriptorTableSlot const& table = m_State->m_DescriptorTables[row.Table.Slot];
			native[index] = { table.Descriptor.Type, table.Native,
							 row.UsedCount != 0 ? Span<const uint32>{owner->UsedEntries.Data() + row.UsedOffset, row.UsedCount}
												: Span<const uint32>{} };
		}
	}

	[[nodiscard]] Result<void> Device::validateDeviceHeapUses(
		CommandSlot& command,
		const BoundDescriptorTables* owner,
		const PreparedRecordingPhase* phase,
		usize additionalUses)
	{
		if (owner == nullptr)
		{
			return Result<void>::Success();
		}

		usize useCount = owner->UsedEntries.Size() + additionalUses;
		const uint32 slot = static_cast<uint32>(&command - m_State->m_Commands.Data());
		for (rhi::BoundDescriptorTableRange const& row : owner->Ranges)
		{
			rhi::DescriptorTableSlot const& table = m_State->m_DescriptorTables[row.Table.Slot];
			if (table.RecordedUses == NumericLimits<usize>::max())
			{
				return preparedFailure(EErrorCode::ExternalFailure);
			}
			for (uint32 index = 0; index < row.UsedCount; ++index)
			{
				rhi::DescriptorTableEntry const& entry = table.Entries[owner->UsedEntries[row.UsedOffset + index]];
				if (entry.Buffer.IsValid())
				{
					rhi::BufferSlot* buffer = phase != nullptr ? &m_State->m_Buffers[entry.Buffer.Slot] : findBuffer(entry.Buffer);
					const EBufferState state = table.Descriptor.Type == EDescriptorType::UniformBuffer ? EBufferState::Uniform
						: table.Descriptor.Type == EDescriptorType::ReadOnlyBuffer
						? EBufferState::ReadOnlyStorage
						: EBufferState::Storage;
					const uint64 bytes = entry.Range.ByteCount != 0 ? entry.Range.ByteCount : (buffer ? buffer->ByteCount - entry.Range.ByteOffset : 0);
					ASSERT(buffer != nullptr && !buffer->bMapped && (phase == nullptr || preparedBufferContains(*phase, entry.Buffer, state, entry.Range.ByteOffset, bytes)),
						"Resource access, aliases, and dependencies must match the prepared command declaration.");
				}
				else if (entry.Texture.IsValid())
				{
					rhi::TextureSlot* texture = phase != nullptr ? &m_State->m_Textures[entry.Texture.Slot] : findTexture(entry.Texture);
					const bool bStorage = table.Descriptor.Type == EDescriptorType::StorageTexture;
					const TextureViewDesc* view = entry.View.IsValid() ? &m_State->m_TextureViews[entry.View.Slot].Descriptor : nullptr;
					if (texture == nullptr ||
						(phase == nullptr && (bStorage ? texture->RecordingSlot != UINT32_MAX && texture->RecordingSlot != slot : !canReadTexture(*texture, slot))) ||
						(phase != nullptr &&
								!preparedTextureContains(*phase, entry.Texture, bStorage ? ETextureState::Storage : ETextureState::ShaderRead, view) &&
								(bStorage || !preparedTextureContains(*phase, entry.Texture, ETextureState::DepthRead, view))))
					{
						return preparedFailure(EErrorCode::Conflict);
					}
				}
				else if (entry.AccelerationStructure.IsValid())
				{
					AccelStructSlot* accel = findAccelStruct(entry.AccelerationStructure);
					if (accel == nullptr || !accel->bBuilt || accel->bUnavailable || (phase != nullptr && !preparedAccelContains(*phase, entry.AccelerationStructure, EAccelStructState::ShaderRead)))
					{
						return preparedFailure(EErrorCode::Conflict);
					}
					if (entry.BottomReferences)
					{
						useCount += entry.BottomReferences->Size();
						for (rhi::AccelStructHandle const child : *entry.BottomReferences)
						{
							ASSERT(phase == nullptr || preparedAccelContains(*phase, child, EAccelStructState::ShaderRead),
								"Resource access, aliases, and dependencies must match the prepared command declaration.");
						}
					}
				}
			}
		}

		if (phase != nullptr)
		{
			return Result<void>::Success();
		}

		if (!command.DescriptorTableUses.Reserve(command.DescriptorTableUses.Size() + owner->Ranges.Size() + 38) ||
			!command.BufferUses.Reserve(command.BufferUses.Size() + useCount) ||
			!command.TextureUses.Reserve(command.TextureUses.Size() + useCount) ||
			!command.SamplerUses.Reserve(command.SamplerUses.Size() + useCount) ||
			!command.AccelUses.Reserve(command.AccelUses.Size() + useCount))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::validateOrdinaryDeviceHeapUses(
		CommandSlot& command,
		const BoundDescriptorTables* owner,
		Span<const BindingSetHandle> sets,
		Span<const DescriptorTableHandle> named,
		Span<const TextureHandle> sampled,
		Span<const TextureHandle> storage)
	{
		if (owner == nullptr)
		{
			return Result<void>::Success();
		}
		ASSERT(sets.Size() <= MAXIMUM_FIXED_BINDING_LAYOUTS &&
			sampled.Size() <= 8 &&
			storage.Size() <= 8,
			"Prepared command arguments must match their declared resources, ranges, and execution order.");

		// Fixed tuples, inline geometry and argument buffers have bounded native envelopes.
		usize additionalUses = MAXIMUM_FIXED_BINDING_LAYOUTS * 32 + 6;
		for (const rhi::BindingSetHandle handle : sets)
		{
			const BindingSetSlot* set = findBindingSet(handle);
			ASSERT(set != nullptr, "Prepared command arguments must match their declared resources, ranges, and execution order.");
			if (!set->Tables)
			{
				continue;
			}
			additionalUses += set->Tables->UsedEntries.Size();
			for (rhi::BoundDescriptorTableRange const& row : set->Tables->Ranges)
			{
				for (uint32 index = 0; index < row.UsedCount; ++index)
				{
					rhi::DescriptorTableEntry const& entry = m_State->m_DescriptorTables[row.Table.Slot].Entries[set->Tables->UsedEntries[row.UsedOffset + index]];
					if (entry.BottomReferences)
					{
						additionalUses += entry.BottomReferences->Size();
					}
				}
			}
		}
		for (const rhi::DescriptorTableHandle handle : named)
		{
			if (!handle.IsValid())
			{
				continue;
			}
			const DescriptorTableSlot* table = findDescriptorTable(handle);
			ASSERT(table != nullptr, "Prepared command arguments must match their declared resources, ranges, and execution order.");
			additionalUses += table->Entries.Size();
		}

		Array<PreparedBufferAccessDesc> bufferStates(m_State->m_Resource);
		Array<PreparedTextureAccessDesc> textureStates(m_State->m_Resource);
		const usize maximumStates = additionalUses + owner->UsedEntries.Size();
		if (!bufferStates.Reserve(maximumStates) || !textureStates.Reserve(maximumStates))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}

		bool bTextureConflict = false;
		const auto addTexture = [&](TextureHandle texture, bool bStorage, TextureViewHandle view = {})
			{
				if (!texture.IsValid())
				{
					return;
				}
				ETextureState state = bStorage ? ETextureState::Storage : ETextureState::ShaderRead;
				if (command.bRendering)
				{
					for (uint32 color = 0; color < command.ColorCount; ++color)
					{
						bTextureConflict |= command.ColorTargets[color] == texture;
					}
					if (command.DepthTarget == texture)
					{
						const ETextureAspect aspect = view.IsValid() ? m_State->m_TextureViews[view.Slot].Descriptor.Aspect : ETextureAspect::Depth;
						bTextureConflict |= bStorage || !(aspect == ETextureAspect::Stencil ? command.bReadOnlyStencil : command.bReadOnlyDepth);
						state = ETextureState::DepthRead;
					}
				}
				PreparedTextureAccessDesc access;
				access.Texture = texture;
				access.State = state;
				if (view.IsValid())
				{
					const TextureViewDesc& range = m_State->m_TextureViews[view.Slot].Descriptor;
					access.BaseMipLevel = range.BaseMipLevel;
					access.MipLevelCount = range.MipLevelCount;
					access.BaseArrayLayer = range.BaseArrayLayer;
					access.ArrayLayerCount = range.ArrayLayerCount;
					access.Aspect = range.Aspect;
				}
				textureStates.AddRequired(access);
			};
		const auto addEntry = [&](const DescriptorTableEntry& entry, EDescriptorType type)
			{
				if (entry.Texture.IsValid())
				{
					addTexture(entry.Texture, type == EDescriptorType::StorageTexture, entry.View);
				}
				if (!entry.Buffer.IsValid())
				{
					return;
				}
				bufferStates.AddRequired({ entry.Buffer,
										  {},
										  type == EDescriptorType::UniformBuffer ? EBufferState::Uniform
										  : type == EDescriptorType::ReadOnlyBuffer ? EBufferState::ReadOnlyStorage
																					: EBufferState::Storage });
			};
		for (rhi::BoundDescriptorTableRange const& row : owner->Ranges)
		{
			rhi::DescriptorTableSlot const& table = m_State->m_DescriptorTables[row.Table.Slot];
			for (uint32 index = 0; index < row.UsedCount; ++index)
			{
				addEntry(table.Entries[owner->UsedEntries[row.UsedOffset + index]], table.Descriptor.Type);
			}
		}
		for (const rhi::BindingSetHandle handle : sets)
		{
			rhi::BindingSetSlot const& set = m_State->m_BindingSets[handle.Slot];
			rhi::BindingLayoutSlot const& layout = m_State->m_BindingLayouts[set.Layout.Slot];
			for (uint32 index = 0; index < set.BufferCount; ++index)
			{
				const uint32 type = layout.Kind == EBindingLayoutKind::Compute
					? static_cast<uint32>(layout.ComputeBuffer(index).Type)
					: static_cast<uint32>(layout.GraphicsBuffer(index).Type);
				bufferStates.AddRequired({ set.Buffers[index],
										  {},
										  type == 0 ? EBufferState::Uniform
										  : type == 2 ? EBufferState::ReadOnlyStorage
													  : EBufferState::Storage });
			}
			for (uint32 index = 0; index < set.TextureCount; ++index)
			{
				addTexture(set.SampledTextures[index], false, set.SampledViews[index]);
			}
			for (uint32 index = 0; index < set.StorageTextureCount; ++index)
			{
				addTexture(set.StorageTextures[index], true, set.StorageViews[index]);
			}
			if (set.Tables)
			{
				for (rhi::BoundDescriptorTableRange const& row : set.Tables->Ranges)
				{
					rhi::DescriptorTableSlot const& table = m_State->m_DescriptorTables[row.Table.Slot];
					for (uint32 index = 0; index < row.UsedCount; ++index)
					{
						addEntry(table.Entries[set.Tables->UsedEntries[row.UsedOffset + index]], table.Descriptor.Type);
					}
				}
			}
		}
		for (const rhi::DescriptorTableHandle handle : named)
		{
			if (handle.IsValid())
			{
				rhi::DescriptorTableSlot const& table = m_State->m_DescriptorTables[handle.Slot];
				for (rhi::DescriptorTableEntry const& entry : table.Entries)
				{
					addEntry(entry, table.Descriptor.Type);
				}
			}
		}
		for (const rhi::TextureHandle texture : sampled)
		{
			addTexture(texture, false);
		}
		for (const rhi::TextureHandle texture : storage)
		{
			addTexture(texture, true);
		}
		ASSERT(!bTextureConflict, "Resource access, aliases, and dependencies must match the prepared command declaration.");
		if (textureStates.Size() > 1)
		{
			Sort(textureStates.begin(), textureStates.end(),
				[](const PreparedTextureAccessDesc& left, const PreparedTextureAccessDesc& right) noexcept
				{
					if (left.Texture.Owner != right.Texture.Owner)
					{
						return left.Texture.Owner < right.Texture.Owner;
					}
					if (left.Texture.Slot != right.Texture.Slot)
					{
						return left.Texture.Slot < right.Texture.Slot;
					}

					return left.Texture.Generation < right.Texture.Generation;
				});
			for (usize index = 1; index < textureStates.Size(); ++index)
			{
				ASSERT(textureStates[index - 1].Texture != textureStates[index].Texture || textureStates[index - 1].State == textureStates[index].State,
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
			}
		}
		if (bufferStates.Size() > 1)
		{
			Sort(bufferStates.begin(), bufferStates.end(),
				[](const PreparedBufferAccessDesc& left, const PreparedBufferAccessDesc& right) noexcept
				{
					if (left.Buffer.Owner != right.Buffer.Owner)
					{
						return left.Buffer.Owner < right.Buffer.Owner;
					}
					if (left.Buffer.Slot != right.Buffer.Slot)
					{
						return left.Buffer.Slot < right.Buffer.Slot;
					}

					return left.Buffer.Generation < right.Buffer.Generation;
				});
			for (usize index = 1; index < bufferStates.Size(); ++index)
			{
				ASSERT(bufferStates[index - 1].Buffer != bufferStates[index].Buffer || bufferStates[index - 1].State == bufferStates[index].State,
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
			}
		}

		return validateDeviceHeapUses(command, owner, nullptr, additionalUses);
	}

	void Device::trackDeviceHeapUses(CommandSlot& command, const BoundDescriptorTables* owner) noexcept
	{
		if (owner != nullptr)
		{
			for (rhi::BoundDescriptorTableRange const& row : owner->Ranges)
			{
				trackBoundTableUses(command, row, *owner);
			}
		}
	}

	[[nodiscard]] Result<void> Device::recordDescriptorHeapBindings(CommandSlot& command, bool bGraphics, const BoundDescriptorTables* owner)
	{
		if (owner == nullptr)
		{
			return Result<void>::Success();
		}
		NativeDescriptorHeapBinding native[32]{};
		fillNativeDeviceHeaps(owner, native);
		ASSERT(backend::HAS_RECORD_DEVICE_HEAP_USES, "Enabled device heaps have a native recorder.");

		return backend::RecordDeviceHeapUses(m_State->m_Backend.State, command.Native, bGraphics, { native, owner->Ranges.Size() });
	}

	[[nodiscard]] Result<SharedPtr<BoundDescriptorTables>> Device::prepareBoundDescriptorTables(const BindingLayoutSlot& layout, Span<const BindingTable> bindings)
	{
		using R = Result<SharedPtr<BoundDescriptorTables>>;
		const auto fail = [](EErrorCode code)
			{
				Error error;
				error.Code = code;

				return R::Failure(Move(error));
			};
		uint32 expected = 0;
		for (uint32 index = 0; index < layout.RangeCount; ++index)
		{
			expected += layout.Range(index).Source == EBindingRangeSource::DescriptorTable ? 1u : 0u;
		}
		ASSERT(bindings.Size() == expected, "Every table range requires exactly one binding.");
		if (bindings.IsEmpty())
		{
			return R::Success({});
		}

		uint32 usedCapacity = 0;
		uint32 maskCapacity = 0;
		for (usize index = 0; index < bindings.Size(); ++index)
		{
			const rhi::BindingTable& binding = bindings[index];
			ASSERT(binding.RangeIndex < layout.RangeCount, "A table range index is out of bounds.");
			const BindingRangeDesc& range = layout.Range(binding.RangeIndex);
			DescriptorTableSlot* table = findDescriptorTable(binding.Table);
			ASSERT(table != nullptr, "RHI operations require valid handles for this device.");
			ASSERT(range.Source == EBindingRangeSource::DescriptorTable &&
				table->Descriptor.Type == range.Type &&
				table->Descriptor.Capacity == range.DescriptorCount &&
				table->Descriptor.BufferFormat == range.BufferFormat &&
				table->Descriptor.StructureStrideBytes == range.StructureStrideBytes &&
				binding.UsedEntries.Size() <= range.DescriptorCount,
				"A table binding must match its declared range.");
			if (table->BoundReferences > UINT32_MAX - expected)
			{
				return fail(EErrorCode::ExternalFailure);
			}
			for (usize previous = 0; previous < index; ++previous)
			{
				ASSERT(bindings[previous].RangeIndex != binding.RangeIndex, "A table range cannot be bound twice.");
			}
			maskCapacity += range.DescriptorCount;
			usedCapacity += binding.UsedEntries.IsEmpty() ? range.DescriptorCount : static_cast<uint32>(binding.UsedEntries.Size());
			for (rhi::DescriptorTableEntry const& entry : table->Entries)
			{
				ASSERT(entry.Buffer.IsValid() || entry.Texture.IsValid() || entry.Sampler.IsValid() || entry.AccelerationStructure.IsValid(), "A bound descriptor table must be populated.");
			}
		}

		SharedPtr<BoundDescriptorTables> owner;
		if (!MakeSharedInto(m_State->m_Resource, &owner, m_State->m_Resource) || !owner->Ranges.Reserve(bindings.Size()) || !owner->UsedEntries.Reserve(usedCapacity) || !owner->UsedMask.Reserve(maskCapacity))
		{
			return fail(EErrorCode::ExternalFailure);
		}

		for (const rhi::BindingTable& binding : bindings)
		{
			const uint32 count = layout.Range(binding.RangeIndex).DescriptorCount;
			const uint32 offset = static_cast<uint32>(owner->UsedEntries.Size());
			Array<Byte> selected(m_State->m_Resource);
			if (!selected.Resize(count))
			{
				return fail(EErrorCode::ExternalFailure);
			}

			const uint32 usedCount = binding.UsedEntries.IsEmpty() ? count : static_cast<uint32>(binding.UsedEntries.Size());
			for (uint32 used = 0; used < usedCount; ++used)
			{
				const uint32 entry = binding.UsedEntries.IsEmpty() ? used : binding.UsedEntries[used];
				ASSERT(entry < count, "A used table entry is out of bounds.");
				ASSERT(selected[entry] == Byte{}, "A used table entry cannot be repeated.");
				selected[entry] = Byte{ 1 };
				owner->UsedEntries.AddRequired(entry);
			}

			const uint32 maskOffset = static_cast<uint32>(owner->UsedMask.Size());
			for (Byte const bit : selected)
			{
				owner->UsedMask.AddRequired(bit);
			}
			owner->Ranges.AddRequired({ binding.RangeIndex, binding.Table, offset, usedCount, maskOffset });
		}

		return R::Success(Move(owner));
	}

	[[nodiscard]] const BoundDescriptorTableRange* Device::findBoundDescriptorTable(const BindingSetSlot& set, uint32 rangeIndex) const noexcept
	{
		if (set.Tables)
		{
			for (rhi::BoundDescriptorTableRange const& row : set.Tables->Ranges)
			{
				if (row.RangeIndex == rangeIndex)
				{
					return &row;
				}
			}
		}

		return nullptr;
	}

	[[nodiscard]] uint32 Device::nativeFixedDescriptorCount(const BindingLayoutSlot& layout) noexcept
	{
		uint32 count = 0;
		for (uint32 index = 0; index < layout.RangeCount; ++index)
		{
			if (layout.Range(index).Source == EBindingRangeSource::Fixed)
			{
				count += layout.Range(index).DescriptorCount;
			}
		}

		return count;
	}

	void Device::trackDescriptorTableOwner(CommandSlot& command, DescriptorTableHandle handle) noexcept
	{
		rhi::DescriptorTableSlot& table = m_State->m_DescriptorTables[handle.Slot];
		for (rhi::RecordedDescriptorTableUse const& use : command.DescriptorTableUses)
		{
			if (use.Handle == handle && use.Native == table.Native)
			{
				return;
			}
		}
		const bool bAdded = command.DescriptorTableUses.Add({ handle, table.Native });
		ASSERT(bAdded, "Descriptor table owner capacity was prepared before recording.");
		++table.RecordedUses;
	}

	[[nodiscard]] Result<void> Device::prepareOrdinaryBoundTableUses(CommandSlot& command, Span<const BindingSetHandle> handles)
	{
		using R = Result<void>;
		const auto fail = [](EErrorCode code)
			{
				Error error;
				error.Code = code;

				return R::Failure(Move(error));
			};
		usize count = 0;
		bool bTableTextures = false;
		for (const rhi::BindingSetHandle handle : handles)
		{
			rhi::BindingSetSlot const& set = m_State->m_BindingSets[handle.Slot];
			if (set.Tables)
			{
				count += set.Tables->UsedEntries.Size();
				for (rhi::BoundDescriptorTableRange const& row : set.Tables->Ranges)
				{
					for (uint32 used = 0; used < row.UsedCount; ++used)
					{
						rhi::DescriptorTableEntry const& entry = m_State->m_DescriptorTables[row.Table.Slot].Entries[set.Tables->UsedEntries[row.UsedOffset + used]];
						bTableTextures |= entry.Texture.IsValid();
						if (entry.BottomReferences)
						{
							count += entry.BottomReferences->Size();
						}
					}
				}
			}
		}
		if (!command.BufferUses.Reserve(command.BufferUses.Size() + count) ||
			!command.TextureUses.Reserve(command.TextureUses.Size() + count) ||
			!command.SamplerUses.Reserve(command.SamplerUses.Size() + count) ||
			!command.AccelUses.Reserve(command.AccelUses.Size() + count))
		{
			return fail(EErrorCode::ExternalFailure);
		}

		Array<PreparedBufferAccessDesc> bufferStates(m_State->m_Resource);
		struct TextureAccess final
		{
			TextureHandle Texture;
			TextureViewHandle View;
			bool bStorage = false;
		};
		Array<TextureAccess> textureStates(m_State->m_Resource);
		if (!bufferStates.Reserve(count + handles.Size() * 8) || (bTableTextures && !textureStates.Reserve(count + handles.Size() * 8)))
		{
			return fail(EErrorCode::ExternalFailure);
		}
		for (const rhi::BindingSetHandle handle : handles)
		{
			rhi::BindingSetSlot const& set = m_State->m_BindingSets[handle.Slot];
			rhi::BindingLayoutSlot const& layout = m_State->m_BindingLayouts[set.Layout.Slot];
			for (uint32 index = 0; index < set.BufferCount; ++index)
			{
				const uint32 type = layout.Kind == EBindingLayoutKind::Compute
					? static_cast<uint32>(layout.ComputeBuffer(index).Type)
					: static_cast<uint32>(layout.GraphicsBuffer(index).Type);
				bufferStates.AddRequired({ set.Buffers[index],
										  {},
										  type == 0 ? EBufferState::Uniform
										  : type == 2 ? EBufferState::ReadOnlyStorage
													  : EBufferState::Storage });
			}
			for (uint32 index = 0; bTableTextures && index < set.TextureCount; ++index)
			{
				textureStates.AddRequired({ set.SampledTextures[index], set.SampledViews[index], false });
			}
			for (uint32 index = 0; bTableTextures && index < set.StorageTextureCount; ++index)
			{
				textureStates.AddRequired({ set.StorageTextures[index], set.StorageViews[index], true });
			}
		}
		for (const BindingSetHandle handle : handles)
		{
			const BindingSetSlot& set = m_State->m_BindingSets[handle.Slot];
			if (!set.Tables)
			{
				continue;
			}
			for (rhi::BoundDescriptorTableRange const& row : set.Tables->Ranges)
			{
				rhi::DescriptorTableSlot& table = m_State->m_DescriptorTables[row.Table.Slot];
				if (table.RecordedUses == NumericLimits<usize>::max())
				{
					return fail(EErrorCode::ExternalFailure);
				}
				for (uint32 used = 0; used < row.UsedCount; ++used)
				{
					rhi::DescriptorTableEntry const& entry = table.Entries[set.Tables->UsedEntries[row.UsedOffset + used]];
					if (entry.Buffer.IsValid())
					{
						BufferSlot* buffer = findBuffer(entry.Buffer);
						ASSERT(buffer && !buffer->bMapped, "Resource access, aliases, and dependencies must match the prepared command declaration.");
						bufferStates.AddRequired({ entry.Buffer,
												  {},
												  table.Descriptor.Type == EDescriptorType::UniformBuffer ? EBufferState::Uniform
												  : table.Descriptor.Type == EDescriptorType::ReadOnlyBuffer ? EBufferState::ReadOnlyStorage
																											 : EBufferState::Storage });
					}
					if (entry.Texture.IsValid())
					{
						if (command.bRendering)
						{
							for (uint32 color = 0; color < command.ColorCount; ++color)
							{
								ASSERT(command.ColorTargets[color] != entry.Texture, "A texture descriptor cannot alias a writable rendering attachment.");
							}
							if (command.DepthTarget == entry.Texture)
							{
								const ETextureAspect aspect = entry.View.IsValid() ? m_State->m_TextureViews[entry.View.Slot].Descriptor.Aspect : ETextureAspect::Depth;
								ASSERT(table.Descriptor.Type == EDescriptorType::SampledTexture && (aspect == ETextureAspect::Stencil ? command.bReadOnlyStencil : command.bReadOnlyDepth),
									"A texture descriptor cannot alias a writable rendering attachment.");
							}
						}
						TextureSlot* texture = findTexture(entry.Texture);
						if (!texture ||
							(table.Descriptor.Type == EDescriptorType::SampledTexture
							? !canReadTexture(*texture, static_cast<uint32>(&command - m_State->m_Commands.Data()))
							: texture->RecordingSlot != UINT32_MAX &&
							texture->RecordingSlot != static_cast<uint32>(&command - m_State->m_Commands.Data())))
						{
							return fail(EErrorCode::Conflict);
						}
						textureStates.AddRequired({ entry.Texture, entry.View,
										   table.Descriptor.Type == EDescriptorType::StorageTexture });
					}
					if (entry.AccelerationStructure.IsValid())
					{
						AccelStructSlot* accel = findAccelStruct(entry.AccelerationStructure);
						if (!accel || !accel->bBuilt || accel->bUnavailable)
						{
							return fail(EErrorCode::Conflict);
						}
					}
				}
			}
		}
		if (textureStates.Size() > 1)
		{
			Sort(textureStates.begin(), textureStates.end(),
				[](const TextureAccess& left, const TextureAccess& right)
				{
					if (left.Texture.Slot != right.Texture.Slot)
					{
						return left.Texture.Slot < right.Texture.Slot;
					}

					return left.bStorage < right.bStorage;
				});
			usize firstTexture = 0;
			for (usize index = 1; index < textureStates.Size(); ++index)
			{
				const TextureAccess& access = textureStates[index];
				if (access.Texture != textureStates[firstTexture].Texture)
				{
					firstTexture = index;
				}
				if (!access.bStorage)
				{
					continue;
				}
				for (usize previous = firstTexture; previous < index && !textureStates[previous].bStorage;
					++previous)
				{
					ASSERT(textureViewsDisjoint(access.View, textureStates[previous].View), "Sampled and storage texture bindings must use disjoint subresources.");
				}
			}
		}

		Sort(bufferStates.Data(), bufferStates.Data() + bufferStates.Size(),
			[](const PreparedBufferAccessDesc& left, const PreparedBufferAccessDesc& right)
			{
				if (left.Buffer.Owner != right.Buffer.Owner)
				{
					return left.Buffer.Owner < right.Buffer.Owner;
				}
				if (left.Buffer.Slot != right.Buffer.Slot)
				{
					return left.Buffer.Slot < right.Buffer.Slot;
				}

				return left.Buffer.Generation < right.Buffer.Generation;
			});
		for (usize index = 1; index < bufferStates.Size(); ++index)
		{
			ASSERT(bufferStates[index - 1].Buffer != bufferStates[index].Buffer || bufferStates[index - 1].State == bufferStates[index].State,
				"Bindings sharing a buffer must require the same whole-buffer state.");
		}

		return R::Success();
	}

	void Device::trackBoundTableUses(CommandSlot& command, const BoundDescriptorTableRange& row, const BoundDescriptorTables& owner) noexcept
	{
		rhi::DescriptorTableSlot& table = m_State->m_DescriptorTables[row.Table.Slot];
		trackDescriptorTableOwner(command, row.Table);
		for (uint32 used = 0; used < row.UsedCount; ++used)
		{
			rhi::DescriptorTableEntry const& entry = table.Entries[owner.UsedEntries[row.UsedOffset + used]];
			if (entry.Buffer.IsValid())
			{
				rhi::BufferSlot& buffer = m_State->m_Buffers[entry.Buffer.Slot];
				const bool bAdded = command.AddBufferUse({ entry.Buffer, buffer.Native, BufferUseState(table.Descriptor.Type) });
				ASSERT(bAdded, "Used table resources were reserved before command emission.");
				++buffer.RecordedUses;
			}
			else if (entry.Texture.IsValid())
			{
				rhi::TextureSlot& texture = m_State->m_Textures[entry.Texture.Slot];
				trackTextureReadOrWrite(command, static_cast<uint32>(&command - m_State->m_Commands.Data()), entry.Texture, texture,
					table.Descriptor.Type == EDescriptorType::SampledTexture ? ETrackedResourceState::ShaderRead
					: ETrackedResourceState::Storage);
			}
			else if (entry.Sampler.IsValid())
			{
				const bool bAdded = command.SamplerUses.Add(entry.Sampler);
				ASSERT(bAdded, "Used table samplers were reserved before command emission.");
				++m_State->m_Samplers[entry.Sampler.Slot].RecordedUses;
			}
			else if (entry.AccelerationStructure.IsValid())
			{
				trackAccelStruct(command, entry.AccelerationStructure, &m_State->m_AccelStructs[entry.AccelerationStructure.Slot]);
				if (entry.BottomReferences)
				{
					for (rhi::AccelStructHandle const child : *entry.BottomReferences)
					{
						trackAccelStruct(command, child, &m_State->m_AccelStructs[child.Slot]);
					}
				}
			}
		}
	}
}
