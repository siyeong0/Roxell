#include "CanonicalArtifactOrder.h"

#include "ShaderMetadata.h"
#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		bool entryLess(const ShaderEntryPoint& left, const ShaderEntryPoint& right)
		{
			return left.Name == right.Name ? left.Stage < right.Stage : left.Name.View() < right.Name.View();
		}

		bool bindingLess(const ShaderBinding& left, const ShaderBinding& right)
		{
			if (left.Space != right.Space)
			{
				return left.Space < right.Space;
			}

			if (left.RegisterNamespace != right.RegisterNamespace)
			{
				return left.RegisterNamespace < right.RegisterNamespace;
			}

			return left.Binding != right.Binding ? left.Binding < right.Binding : left.Name.View() < right.Name.View();
		}

		bool makeMap(const Array<uint32>& order, Array<uint32>& map)
		{
			if (!map.Resize(order.Size()))
			{
				return false;
			}

			for (uint32 i = 0; i < order.Size(); ++i)
			{
				map[order[i]] = i;
			}

			return true;
		}

		Result<Hash256> computeLayoutKeyRecursive(const ShaderVariant& variant, uint32 index, MemoryResource& resource, Array<Hash256>& keys, Array<uint8>& complete)
		{
			if (complete[index] != 0)
			{
				return Result<Hash256>::Success(keys[index]);
			}

			Writer writer(resource);
			const ShaderDataLayout& layout = variant.Layouts[index];
			writer.U64(layout.ByteSize);
			writer.U32(layout.Alignment);
			writer.U64(layout.ElementStride);
			writer.U32(static_cast<uint32>(layout.Fields.Size()));
			Array<uint32> fields(resource);
			if (!OrderIndices(layout.Fields, fields, FieldLess))
			{
				return Result<Hash256>::Failure(AllocationError());
			}

			for (uint32 fieldIndex : fields)
			{
				const ShaderDataField& field = layout.Fields[fieldIndex];
				WriteField(writer, field, field.NestedLayout == NO_DATA_LAYOUT ? NO_DATA_LAYOUT : 0);
				if (field.NestedLayout != NO_DATA_LAYOUT)
				{
					Result<Hash256> key = computeLayoutKeyRecursive(variant, field.NestedLayout, resource, keys, complete);
					if (!key)
					{
						return key;
					}

					writer.Hash(key.Value());
				}
			}

			if (!writer.IsValid())
			{
				return Result<Hash256>::Failure(AllocationError());
			}

			keys[index] = ComputeSha256({writer.GetBytes().Data(), writer.GetBytes().Size()});
			complete[index] = 1;
			return Result<Hash256>::Success(keys[index]);
		}
	} // namespace

	CanonicalArtifactOrder::CanonicalArtifactOrder(MemoryResource& resource) noexcept
		: m_Entries(resource)
		, m_EntryMap(resource)
		, m_Variants(resource)
		, m_VariantOrders(resource)
	{
	}

	Result<void> CanonicalArtifactOrder::Initialize(const ShaderBuilder& artifact, CancellationToken cancellation)
	{
		using R = Result<void>;
		ASSERT(m_Entries.IsEmpty() && m_VariantOrders.IsEmpty(), "Canonical artifact order initializes once.");
		MemoryResource& resource = m_Entries.GetResource();

		if (!OrderIndices(artifact.EntryPoints, m_Entries, entryLess) || !makeMap(m_Entries, m_EntryMap) || !OrderIndices(artifact.Variants, m_Variants,
		        [&](const ShaderVariant& left, const ShaderVariant& right)
		        {
			        const uint32 leftEntry = m_EntryMap[left.EntryPointIndex];
			        const uint32 rightEntry = m_EntryMap[right.EntryPointIndex];
			        if (leftEntry != rightEntry)
			        {
				        return leftEntry < rightEntry;
			        }

			        return left.Target < right.Target;
		        }))
		{
			return R::Failure(AllocationError());
		}

		if (!m_VariantOrders.Reserve(artifact.Variants.Size()))
		{
			return R::Failure(AllocationError());
		}

		for (const ShaderVariant& variant : artifact.Variants)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(CancelledError());
			}

			Array<Hash256> layoutKeys(resource);
			Array<uint8> complete(resource);
			if (!layoutKeys.Resize(variant.Layouts.Size()) || !complete.Resize(variant.Layouts.Size()))
			{
				return R::Failure(AllocationError());
			}

			for (uint32 layoutIndex = 0; layoutIndex < variant.Layouts.Size(); ++layoutIndex)
			{
				if (cancellation.IsCancellationRequested())
				{
					return R::Failure(CancelledError());
				}

				Result<Hash256> key = computeLayoutKeyRecursive(variant, layoutIndex, resource, layoutKeys, complete);
				if (!key)
				{
					return R::Failure(key.GetError());
				}
			}

			CanonicalVariantOrder order(resource);
			Array<uint32>& layouts = order.Layouts;
			Array<uint32>& layoutMap = order.LayoutMap;
			Array<uint32>& bindings = order.Bindings;
			Array<uint32>& bindingMap = order.BindingMap;
			Array<uint32>& uses = order.Uses;
			if (!OrderIndices(layoutKeys, layouts, [](const Hash256& left, const Hash256& right) { return left < right; }) ||
				!layoutMap.Resize(layouts.Size()) ||
				!OrderIndices(variant.Bindings, bindings, bindingLess) || !makeMap(bindings, bindingMap) ||
				!OrderIndices(variant.ResourceUses, uses, [&](const ShaderResourceUse& left, const ShaderResourceUse& right) { return bindingMap[left.BindingIndex] < bindingMap[right.BindingIndex]; }))
			{
				return R::Failure(AllocationError());
			}

			Array<uint32> uniqueLayouts(resource);
			for (uint32 layoutIndex : layouts)
			{
				if (uniqueLayouts.IsEmpty() || layoutKeys[uniqueLayouts.Back()] != layoutKeys[layoutIndex])
				{
					if (!uniqueLayouts.Add(layoutIndex))
					{
						return R::Failure(AllocationError());
					}
				}

				layoutMap[layoutIndex] = static_cast<uint32>(uniqueLayouts.Size() - 1);
			}

			// Equal structural keys coalesce only after confirming their canonical bodies agree.
			for (uint32 layoutIndex : layouts)
			{
				const uint32 representative = uniqueLayouts[layoutMap[layoutIndex]];
				if (representative == layoutIndex)
				{
					continue;
				}

				Writer left(resource);
				Writer right(resource);
				WriteLayoutBody(left, variant.Layouts[layoutIndex], &layoutMap);
				WriteLayoutBody(right, variant.Layouts[representative], &layoutMap);
				if (!left.IsValid() || !right.IsValid())
				{
					return R::Failure(AllocationError());
				}

				if (left.GetBytes().Size() != right.GetBytes().Size() || CompareBytes(left.GetBytes().Data(), right.GetBytes().Data(), left.GetBytes().Size()) != 0)
				{
					return R::Failure(MakeArtifactError("Shader layout structural digest collision."));
				}
			}

			layouts = Move(uniqueLayouts);
			m_VariantOrders.AddRequired(Move(order));
		}

		return R::Success();
	}

	bool CanonicalArtifactOrder::IsCanonical(const ShaderBuilder& artifact) const noexcept
	{
		auto identityOrder = [](const Array<uint32>& indices)
		{
			for (usize index = 0; index < indices.Size(); ++index)
			{
				if (indices[index] != index)
				{
					return false;
				}
			}

			return true;
		};
		if (!identityOrder(m_Entries) || !identityOrder(m_Variants))
		{
			return false;
		}

		for (uint32 index = 0; index < artifact.Variants.Size(); ++index)
		{
			const ShaderVariant& variant = artifact.Variants[index];
			if (!MetadataIsCanonical(variant))
			{
				return false;
			}

			const CanonicalVariantOrder& order = m_VariantOrders[index];
			if (order.Layouts.Size() != variant.Layouts.Size() || !identityOrder(order.Layouts) || !identityOrder(order.Bindings) || !identityOrder(order.Uses))
			{
				return false;
			}

			for (const ShaderDataLayout& layout : variant.Layouts)
			{
				if (layout.Fields.Size() > 1 && !IsSorted(layout.Fields.begin(), layout.Fields.end(), FieldLess))
				{
					return false;
				}
			}
		}

		return true;
	}
} // namespace rox::shader
