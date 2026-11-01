#pragma once

#include <Shader.h>

namespace rox::shader
{
	struct ShaderBufferValue
	{
		core::StringView Field;
		ShaderPropertyValue Value;
	};

	struct ShaderHostLayout
	{
		core::Span<const ShaderDataLayout> Layouts;
		core::uint32 RootLayout = 0;
	};

	struct ShaderPackingLimits
	{
		core::uint64 MaximumBytes = 64u * 1024u * 1024u;
		core::uint32 MaximumOperations = 65536;
		core::uint32 MaximumNestingDepth = 32;
	};

	// Immutable operations retain the target artifact and no borrowed host descriptor storage.
	class ShaderBufferWriter
	{
	public:
		[[nodiscard]] static core::Result<ShaderBufferWriter> Create(
			const Shader&,
			core::uint32 variantIndex,
			core::uint32 layoutIndex,
			ShaderHostLayout,
			core::MemoryResource&,
			const ShaderPackingLimits& = {});

		ShaderBufferWriter() noexcept = default;
		bool IsValid() const noexcept;
		core::uint64 GetSourceByteSize() const noexcept;
		core::uint64 GetDestinationByteSize() const noexcept;
		core::uint32 GetOperationCount() const noexcept;

		// Writes a complete numeric top-level layout. Uses temporary storage; failures leave destination
		// untouched. Nested fields, arrays and scalar64 use WriteStruct with an explicit host layout.
		[[nodiscard]] static core::Result<void> WriteValues(
			const Shader&,
			core::uint32 variantIndex,
			core::uint32 layoutIndex,
			core::Span<const ShaderBufferValue>,
			core::Span<core::Byte> destination,
			core::MemoryResource&);

		// Checks both complete spans before writing; performs no allocation.
		[[nodiscard]] core::Result<void> WriteStruct(core::Span<const core::Byte> source, core::Span<core::Byte> destination) const;

	private:
		struct Operation
		{
			core::uint64 SourceOffset = 0;
			core::uint64 DestinationOffset = 0;
			core::uint32 ByteCount = 0;
		};

		struct Storage
		{
			Shader Artifact;

			core::Array<Operation> Operations;

			core::uint64 SourceByteSize = 0;
			core::uint64 DestinationByteSize = 0;

			explicit Storage(core::MemoryResource& resource) noexcept
				: Operations(resource)
			{
			}
		};

		static core::Result<void> addOperationsRecursive(
			Storage& storage,
			const ShaderVariant& variant,
			core::uint32 targetIndex,
			ShaderHostLayout host,
			core::uint32 hostIndex,
			const core::Array<core::Array<core::uint32>>& names,
			core::uint64 sourceBase,
			core::uint64 destinationBase,
			core::uint32 depth,
			core::uint64& budget,
			const ShaderPackingLimits& limits);

		core::SharedPtr<Storage> m_Storage;
	};

} // namespace rox::shader
