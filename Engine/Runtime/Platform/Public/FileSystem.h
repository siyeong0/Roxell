#pragma once

#include <Array.hpp>
#include <InterfaceOwner.hpp>
#include <MemoryResource.h>
#include <Result.hpp>
#include <Span.hpp>
#include <StringView.hpp>
#include <TypeTraits.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::os
{
	class IFileSystem;

	class ExclusiveFileLock final
	{
	public:
		[[nodiscard]] static core::Result<ExclusiveFileLock> Acquire(IFileSystem& fileSystem, rox::core::StringView relativePath);

		ExclusiveFileLock() noexcept = default;
		~ExclusiveFileLock();

		ExclusiveFileLock(const ExclusiveFileLock&) = delete;
		ExclusiveFileLock& operator=(const ExclusiveFileLock&) = delete;
		ExclusiveFileLock(ExclusiveFileLock&& other) noexcept;
		ExclusiveFileLock& operator=(ExclusiveFileLock&& other) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		void Reset() noexcept;

	private:
		ExclusiveFileLock(IFileSystem& fileSystem, core::uintptr token) noexcept;

	private:
		IFileSystem* m_FileSystem = nullptr;
		core::uintptr m_Token = 0;
	};

	enum class EAtomicWritePolicy : core::uint8
	{
		CreateNew,
		ReplaceExisting
	};

	enum class EAtomicWriteOutcome : core::uint8
	{
		Published,
		AlreadyExists
	};

	class IFileSystem
	{
		friend class ExclusiveFileLock;

	public:
		virtual ~IFileSystem() = default;

		virtual core::Result<bool> Exists(rox::core::StringView relativePath) const = 0;

		virtual core::Result<rox::core::Array<core::Byte>> ReadFile(rox::core::StringView relativePath, core::uint64 maximumBytes) const;

		// The caller's resource owns the returned bytes and must outlive them.
		virtual core::Result<core::Array<core::Byte>> ReadFile(rox::core::StringView relativePath, core::uint64 maximumBytes, core::MemoryResource& resource) const = 0;

		virtual core::Result<EAtomicWriteOutcome> WriteFileAtomically(rox::core::StringView relativePath, rox::core::Span<const core::Byte> data, EAtomicWritePolicy policy) = 0;

		virtual core::Result<void> RemoveFile(rox::core::StringView relativePath) = 0;

		// The lock coordinates cooperating owners that use the same root, including owners in other processes.
		// Acquisition is nonblocking, the returned value releases through RAII, and this filesystem outlives it.
		[[nodiscard]] core::Result<ExclusiveFileLock> TryAcquireExclusiveFileLock(rox::core::StringView relativePath)
		{
			return ExclusiveFileLock::Acquire(*this, relativePath);
		}

	protected:
		virtual core::Result<core::uintptr> tryAcquireExclusiveFileLock(rox::core::StringView relativePath) = 0;
		virtual void releaseExclusiveFileLock(core::uintptr token) noexcept = 0;
	};

	class FileSystemOwner final
	{
	public:
		FileSystemOwner() noexcept = default;
		~FileSystemOwner() noexcept;
		FileSystemOwner(const FileSystemOwner&) = delete;
		FileSystemOwner& operator=(const FileSystemOwner&) = delete;
		FileSystemOwner(FileSystemOwner&& other) noexcept;
		FileSystemOwner& operator=(FileSystemOwner&& other) noexcept;

		template <typename TFileSystem, typename... TArguments>
		[[nodiscard]] static FileSystemOwner TryCreate(core::MemoryResource& resource, TArguments&&... arguments) noexcept
		{
			FileSystemOwner owner;
			owner.m_Owner = core::InterfaceOwner<IFileSystem>::Create<TFileSystem>(resource, rox::core::Forward<TArguments>(arguments)...);

			return owner;
		}

		void Reset() noexcept;
		[[nodiscard]] IFileSystem* Get() const noexcept;
		[[nodiscard]] core::MemoryResource* GetResource() const noexcept;
		[[nodiscard]] explicit operator bool() const noexcept;
		IFileSystem* operator->() const noexcept;

	private:
		core::InterfaceOwner<IFileSystem> m_Owner;
	};

	static_assert(rox::core::IsNothrowMoveConstructible<FileSystemOwner>);
	static_assert(rox::core::IsNothrowMoveAssignable<FileSystemOwner>);
} // namespace rox::os
