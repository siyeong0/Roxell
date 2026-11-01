#include <ShaderBufferWriter.h>

#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <Comparison.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		uint64 getElementOffset(const ShaderDataField& field, uint32 row, uint32 column, uint32 scalarBytes)
		{
			if (field.MatrixOrder == EShaderMatrixOrder::ColumnMajor)
			{
				return uint64(column) * field.MatrixStride + uint64(row) * scalarBytes;
			}

			return uint64(row) * field.MatrixStride + uint64(column) * scalarBytes;
		}

		struct HostVisit
		{
			uint32 Height = 0;
			bool bVisiting = false;
		};

		bool visitHostRecursive(ShaderHostLayout host, uint32 index, uint32 depth, const ShaderPackingLimits& limits, Array<HostVisit>& visits)
		{
			HostVisit& visit = visits[index];
			if (depth > limits.MaximumNestingDepth || visit.bVisiting)
			{
				return false;
			}

			if (visit.Height != 0)
			{
				return visit.Height <= limits.MaximumNestingDepth - depth + 1;
			}

			visit.bVisiting = true;
			uint32 height = 1;
			for (const ShaderDataField& field : host.Layouts[index].Fields)
			{
				if (field.NestedLayout != NO_DATA_LAYOUT)
				{
					if (!visitHostRecursive(host, field.NestedLayout, depth + 1, limits, visits))
					{
						return false;
					}

					height = Max(height, visits[field.NestedLayout].Height + 1);
				}
			}

			visit.Height = height;
			visit.bVisiting = false;
			return height <= limits.MaximumNestingDepth - depth + 1;
		}

		Result<void> validateHost(ShaderHostLayout host, const ShaderPackingLimits& limits, Array<Array<uint32>>& names, MemoryResource& resource)
		{
			using R = Result<void>;
			if (host.Layouts.IsEmpty() || host.Layouts.Size() > 4096 || host.RootLayout >= host.Layouts.Size())
			{
				return R::Failure(MakeArtifactError("Invalid host layout collection or root reference."));
			}

			uint64 totalFields = 0;
			for (const ShaderDataLayout& layout : host.Layouts)
			{
				totalFields += layout.Fields.Size();
				if (totalFields > 65536 || layout.ByteSize == 0 || layout.ByteSize > limits.MaximumBytes || layout.Alignment == 0 || layout.Alignment > 4096 ||
					(layout.Alignment & (layout.Alignment - 1)) != 0 ||
					(layout.ElementStride != 0 && (layout.ElementStride < layout.ByteSize || layout.ElementStride % layout.Alignment != 0)))
				{
					return R::Failure(MakeArtifactError("Invalid host layout extent or alignment."));
				}

				for (const ShaderDataField& field : layout.Fields)
				{
					if (field.Name.IsEmpty() || field.Name.Size() > 4096 || field.Name.View().Contains('\0') || !IsValidUtf8(field.Name.View()) || field.ByteSize == 0 ||
						field.ByteOffset > layout.ByteSize ||
						field.ByteSize > layout.ByteSize - field.ByteOffset ||
						field.Scalar > EShaderScalar::Float ||
						field.MatrixOrder > EShaderMatrixOrder::ColumnMajor ||
						field.VectorWidth == 0 ||
						field.VectorWidth > 4 ||
						field.MatrixRows == 0 ||
						field.MatrixRows > 4 ||
						field.MatrixColumns == 0 ||
						field.MatrixColumns > 4 ||
						field.ArrayCount == 0 ||
						(field.NestedLayout != NO_DATA_LAYOUT && field.NestedLayout >= host.Layouts.Size()))
					{
						return R::Failure(MakeArtifactError("Invalid host data field shape or extent."));
					}

					uint64 element = 0;
					if (field.NestedLayout != NO_DATA_LAYOUT)
					{
						if (field.Scalar != EShaderScalar::None || field.ScalarWidth != 0 || field.VectorWidth != 1 || field.MatrixRows != 1 || field.MatrixColumns != 1 ||
							field.MatrixOrder != EShaderMatrixOrder::None ||
							field.MatrixStride != 0)
						{
							return R::Failure(MakeArtifactError("Nested host field also declares a scalar or matrix shape."));
						}

						element = host.Layouts[field.NestedLayout].ByteSize;
					}
					else
					{
						if (field.Scalar == EShaderScalar::None || (field.ScalarWidth != 8 && field.ScalarWidth != 16 && field.ScalarWidth != 32 && field.ScalarWidth != 64))
						{
							return R::Failure(MakeArtifactError("Invalid host scalar representation."));
						}

						element = uint64(field.ScalarWidth / 8) * field.VectorWidth;
						if (field.MatrixOrder != EShaderMatrixOrder::None)
						{
							const uint32 major = field.MatrixOrder == EShaderMatrixOrder::RowMajor ? field.MatrixRows : field.MatrixColumns;
							const uint32 minor = field.MatrixOrder == EShaderMatrixOrder::RowMajor ? field.MatrixColumns : field.MatrixRows;
							const uint64 vectorBytes = uint64(minor) * (field.ScalarWidth / 8);
							if (field.VectorWidth != 1 || vectorBytes > field.ByteSize || field.MatrixStride < vectorBytes ||
								(major > 1 && field.MatrixStride > (field.ByteSize - vectorBytes) / (major - 1)))
							{
								return R::Failure(MakeArtifactError("Invalid host matrix stride."));
							}

							element = field.MatrixStride * (major - 1) + vectorBytes;
						}
						else if (field.MatrixRows != 1 || field.MatrixColumns != 1 || field.MatrixStride != 0)
						{
							return R::Failure(MakeArtifactError("Non-matrix host field declares matrix dimensions."));
						}
					}

					if (element > field.ByteSize ||
						(field.ArrayCount > 1 && (field.ArrayStride < element || field.ArrayStride > (field.ByteSize - element) / (field.ArrayCount - 1))))
					{
						return R::Failure(MakeArtifactError("Invalid host array stride or extent."));
					}
				}

				Array<uint32> order(resource);
				if (!OrderIndices(layout.Fields, order, FieldLess))
				{
					return R::Failure(AllocationError());
				}

				for (usize index = 1; index < order.Size(); ++index)
				{
					const ShaderDataField& prior = layout.Fields[order[index - 1]];
					const ShaderDataField& current = layout.Fields[order[index]];
					if (current.ByteOffset < prior.ByteOffset + prior.ByteSize)
					{
						return R::Failure(MakeArtifactError("Host fields overlap."));
					}
				}

				if (order.Size() > 1)
				{
					Sort(order.begin(), order.end(), [&](uint32 left, uint32 right) { return layout.Fields[left].Name.View() < layout.Fields[right].Name.View(); });
				}

				for (usize index = 1; index < order.Size(); ++index)
				{
					if (layout.Fields[order[index - 1]].Name == layout.Fields[order[index]].Name)
					{
						return R::Failure(MakeArtifactError("Duplicate host field name."));
					}
				}

				if (!names.Add(Move(order)))
				{
					return R::Failure(AllocationError());
				}
			}

			Array<HostVisit> visits(resource);
			if (!visits.Resize(host.Layouts.Size()))
			{
				return R::Failure(AllocationError());
			}

			for (uint32 index = 0; index < host.Layouts.Size(); ++index)
			{
				if (!visitHostRecursive(host, index, 1, limits, visits))
				{
					return R::Failure(MakeArtifactError("Cyclic or excessively nested host layout."));
				}
			}

			return R::Success();
		}
	} // namespace

	Result<ShaderBufferWriter> ShaderBufferWriter::Create(
		const Shader& artifact,
		uint32 variantIndex,
		uint32 layoutIndex,
		ShaderHostLayout host,
		MemoryResource& resource,
		const ShaderPackingLimits& limits)
	{
		using R = Result<ShaderBufferWriter>;
		if (!artifact.IsValid() || variantIndex >= artifact.GetVariants().Size() || layoutIndex >= artifact.GetVariants()[variantIndex].Layouts.Size() ||
			limits.MaximumBytes == 0 ||
			limits.MaximumBytes > 64u * 1024u * 1024u ||
			limits.MaximumOperations == 0 ||
			limits.MaximumOperations > 1024u * 1024u ||
			limits.MaximumNestingDepth == 0 ||
			limits.MaximumNestingDepth > 32)
		{
			return R::Failure(MakeArtifactError("Invalid shader packing target or limits."));
		}

		Array<Array<uint32>> names(resource);
		Result<void> checked = validateHost(host, limits, names, resource);
		if (!checked)
		{
			return R::Failure(checked.GetError());
		}

		const ShaderVariant& variant = artifact.GetVariants()[variantIndex];
		if (variant.Layouts[layoutIndex].ByteSize > limits.MaximumBytes)
		{
			return R::Failure(MakeArtifactError("Shader packing target exceeds its byte bound."));
		}

		ShaderBufferWriter result;
		if (!MakeSharedInto(resource, &result.m_Storage, resource))
		{
			return R::Failure(AllocationError());
		}

		Storage& storage = *result.m_Storage;
		storage.Artifact = artifact;
		storage.SourceByteSize = host.Layouts[host.RootLayout].ByteSize;
		storage.DestinationByteSize = variant.Layouts[layoutIndex].ByteSize;
		uint64 budget = (uint64(limits.MaximumOperations) + 1) * (uint64(limits.MaximumNestingDepth) + 1);
		Result<void> operations = ShaderBufferWriter::addOperationsRecursive(storage, variant, layoutIndex, host, host.RootLayout, names, 0, 0, 1, budget, limits);
		if (!operations)
		{
			return R::Failure(operations.GetError());
		}

		return R::Success(Move(result));
	}

	bool ShaderBufferWriter::IsValid() const noexcept
	{
		return static_cast<bool>(m_Storage);
	}

	uint64 ShaderBufferWriter::GetSourceByteSize() const noexcept
	{
		ASSERT(IsValid(), "Shader packing plan must be valid.");
		return m_Storage->SourceByteSize;
	}

	uint64 ShaderBufferWriter::GetDestinationByteSize() const noexcept
	{
		ASSERT(IsValid(), "Shader packing plan must be valid.");
		return m_Storage->DestinationByteSize;
	}

	uint32 ShaderBufferWriter::GetOperationCount() const noexcept
	{
		ASSERT(IsValid(), "Shader packing plan must be valid.");
		return static_cast<uint32>(m_Storage->Operations.Size());
	}

	Result<void> ShaderBufferWriter::WriteValues(
		const Shader& shader,
		uint32 variantIndex,
		uint32 layoutIndex,
		Span<const ShaderBufferValue> values,
		Span<Byte> destination,
		MemoryResource& memory)
	{
		using R = Result<void>;
		if (values.Size() > 65536)
		{
			return R::Failure(MakeArtifactError("Too many buffer values."));
		}

		Array<ShaderDataLayout> layouts(memory);
		ShaderDataLayout host(memory);
		host.Alignment = 4;
		Array<Byte> source(memory);
		if (!source.ResizeUninitialized(values.Size() * 64))
		{
			return R::Failure(AllocationError());
		}
		for (const ShaderBufferValue& value : values)
		{
			const EShaderPropertyType type = value.Value.Type;
			if (type >= EShaderPropertyType::Texture2D)
			{
				return R::Failure(MakeArtifactError("Buffer values must be numeric."));
			}

			ShaderDataField field(memory);
			if (!field.Name.Assign(value.Field))
			{
				return R::Failure(AllocationError());
			}
			field.Scalar = EShaderScalar::Float;
			if (type == EShaderPropertyType::Int)
			{
				field.Scalar = EShaderScalar::SignedInteger;
			}
			else if (type == EShaderPropertyType::Bool)
			{
				field.Scalar = EShaderScalar::Bool;
			}

			field.ScalarWidth = 32;
			field.VectorWidth = (type == EShaderPropertyType::Vector || type == EShaderPropertyType::Color) ? 4 : 1;
			field.ByteOffset = host.ByteSize;
			field.ByteSize = 4 * field.VectorWidth;
			if (type == EShaderPropertyType::Matrix)
			{
				field.MatrixRows = field.MatrixColumns = 4;
				field.MatrixOrder = EShaderMatrixOrder::RowMajor;
				field.MatrixStride = 16;
				field.ByteSize = 64;
			}
			if (type == EShaderPropertyType::Bool && value.Value.Bits[0] > 1)
			{
				return R::Failure(MakeArtifactError("Invalid boolean buffer value."));
			}

			CopyBytes(source.Data() + field.ByteOffset, value.Value.Bits, static_cast<usize>(field.ByteSize));
			host.ByteSize += field.ByteSize;
			if (!host.Fields.Add(Move(field)))
			{
				return R::Failure(AllocationError());
			}
		}
		if (!layouts.Add(Move(host)))
		{
			return R::Failure(AllocationError());
		}

		Result<ShaderBufferWriter> writer = Create(shader, variantIndex, layoutIndex, {layouts.View(), 0}, memory);
		if (!writer)
		{
			return R::Failure(writer.GetError());
		}

		return writer.Value().WriteStruct(source.View(), destination);
	}

	Result<void> ShaderBufferWriter::WriteStruct(Span<const Byte> source, Span<Byte> destination) const
	{
		using R = Result<void>;
		if (!IsValid() || source.Size() < GetSourceByteSize() || destination.Size() < GetDestinationByteSize())
		{
			return R::Failure(MakeArtifactError("Invalid packing plan or undersized byte spans."));
		}

		const uintptr sourceStart = reinterpret_cast<uintptr>(source.Data());
		const uintptr destinationStart = reinterpret_cast<uintptr>(destination.Data());
		const bool bOverlap = sourceStart <= destinationStart ? destinationStart - sourceStart < source.Size() : sourceStart - destinationStart < destination.Size();
		if (bOverlap)
		{
			return R::Failure(MakeArtifactError("Shader packing source and destination spans overlap."));
		}

		SetBytes(destination.Data(), 0, static_cast<usize>(GetDestinationByteSize()));
		for (const Operation& operation : m_Storage->Operations)
		{
			CopyBytes(destination.Data() + operation.DestinationOffset, source.Data() + operation.SourceOffset, operation.ByteCount);
		}

		return R::Success();
	}

	Result<void> ShaderBufferWriter::addOperationsRecursive(
		Storage& storage,
		const ShaderVariant& variant,
		uint32 targetIndex,
		ShaderHostLayout host,
		uint32 hostIndex,
		const Array<Array<uint32>>& names,
		uint64 sourceBase,
		uint64 destinationBase,
		uint32 depth,
		uint64& budget,
		const ShaderPackingLimits& limits)
	{
		using R = Result<void>;
		if (depth > limits.MaximumNestingDepth || budget == 0)
		{
			return R::Failure(MakeArtifactError("Shader packing traversal exceeds its configured bound."));
		}
		--budget;
		const ShaderDataLayout& target = variant.Layouts[targetIndex];
		const ShaderDataLayout& native = host.Layouts[hostIndex];
		const Array<uint32>& order = names[hostIndex];
		for (const ShaderDataField& field : target.Fields)
		{
			if (order.IsEmpty())
			{
				return R::Failure(MakeArtifactError("Required host shader field is missing."));
			}

			const uint32* const found = LowerBound(order.begin(), order.end(), field.Name.View(), [&](uint32 index, StringView name) { return native.Fields[index].Name.View() < name; });
			if (found == order.end() || native.Fields[*found].Name != field.Name)
			{
				return R::Failure(MakeArtifactError("Required host shader field is missing."));
			}

			const ShaderDataField& original = native.Fields[*found];
			if (field.Scalar != original.Scalar || field.ScalarWidth != original.ScalarWidth || field.VectorWidth != original.VectorWidth ||
				field.MatrixRows != original.MatrixRows ||
				field.MatrixColumns != original.MatrixColumns ||
				field.ArrayCount != original.ArrayCount ||
				((field.MatrixOrder == EShaderMatrixOrder::None) != (original.MatrixOrder == EShaderMatrixOrder::None)) ||
				((field.NestedLayout == NO_DATA_LAYOUT) != (original.NestedLayout == NO_DATA_LAYOUT)))
			{
				return R::Failure(MakeArtifactError("Host and shader fields have incompatible representations."));
			}

			for (uint32 element = 0; element < field.ArrayCount; ++element)
			{
				if (budget == 0)
				{
					return R::Failure(MakeArtifactError("Shader packing traversal exceeds its configured bound."));
				}
				--budget;
				const uint64 sourceOffset = sourceBase + original.ByteOffset + uint64(element) * original.ArrayStride;
				const uint64 destinationOffset = destinationBase + field.ByteOffset + uint64(element) * field.ArrayStride;
				if (field.NestedLayout != NO_DATA_LAYOUT)
				{
					Result<void> nested = addOperationsRecursive(storage, variant, field.NestedLayout, host, original.NestedLayout, names, sourceOffset, destinationOffset, depth + 1, budget, limits);
					if (!nested)
					{
						return nested;
					}

					continue;
				}

				const uint32 rows = field.MatrixOrder == EShaderMatrixOrder::None ? 1u : field.MatrixRows;
				const uint32 columns = field.MatrixOrder == EShaderMatrixOrder::None ? field.VectorWidth : field.MatrixColumns;
				const uint32 bytes = field.ScalarWidth / 8;
				for (uint32 row = 0; row < rows; ++row)
				{
					for (uint32 column = 0; column < columns; ++column)
					{
						if (storage.Operations.Size() >= limits.MaximumOperations)
						{
							return R::Failure(MakeArtifactError("Shader packing operations exceed their configured bound."));
						}

						Operation operation;
						operation.SourceOffset = sourceOffset + getElementOffset(original, row, column, bytes);
						operation.DestinationOffset = destinationOffset + getElementOffset(field, row, column, bytes);
						operation.ByteCount = bytes;
						if (operation.SourceOffset > storage.SourceByteSize || bytes > storage.SourceByteSize - operation.SourceOffset ||
							operation.DestinationOffset > storage.DestinationByteSize ||
							bytes > storage.DestinationByteSize - operation.DestinationOffset)
						{
							return R::Failure(MakeArtifactError("Shader packing operation exceeds its complete byte extent."));
						}

						if (!storage.Operations.Add(operation))
						{
							return R::Failure(AllocationError());
						}
					}
				}
			}
		}

		return R::Success();
	}

} // namespace rox::shader
