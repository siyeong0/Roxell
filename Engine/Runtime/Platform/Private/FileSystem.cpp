#include <FileSystem.h>
#include <SystemMemoryResource.h>

namespace rox::os
{
	core::Result<core::Array<core::Byte>> IFileSystem::ReadFile(core::StringView relativePath, core::uint64 maximumBytes) const
	{
		return ReadFile(relativePath, maximumBytes, core::SystemMemoryResource::Get());
	}

	core::Result<ExclusiveFileLock> ExclusiveFileLock::Acquire(IFileSystem& fileSystem, const rox::core::StringView relativePath)
	{
		core::Result<core::uintptr> tokenResult = fileSystem.tryAcquireExclusiveFileLock(relativePath);

		if (!tokenResult)
		{
			return core::Result<ExclusiveFileLock>::Failure(tokenResult.GetError());
		}

		return core::Result<ExclusiveFileLock>::Success(ExclusiveFileLock(fileSystem, tokenResult.Value()));
	}

	ExclusiveFileLock::ExclusiveFileLock(IFileSystem& fileSystem, const core::uintptr token) noexcept
		: m_FileSystem(&fileSystem)
		, m_Token(token)
	{
	}

	ExclusiveFileLock::~ExclusiveFileLock()
	{
		Reset();
	}

	ExclusiveFileLock::ExclusiveFileLock(ExclusiveFileLock&& other) noexcept
		: m_FileSystem(other.m_FileSystem)
		, m_Token(other.m_Token)
	{
		other.m_FileSystem = nullptr;
		other.m_Token = 0;
	}

	ExclusiveFileLock& ExclusiveFileLock::operator=(ExclusiveFileLock&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_FileSystem = other.m_FileSystem;
			m_Token = other.m_Token;
			other.m_FileSystem = nullptr;
			other.m_Token = 0;
		}

		return *this;
	}

	bool ExclusiveFileLock::IsValid() const noexcept
	{
		return m_FileSystem != nullptr;
	}

	void ExclusiveFileLock::Reset() noexcept
	{
		IFileSystem* const fileSystem = m_FileSystem;
		const core::uintptr token = m_Token;
		m_FileSystem = nullptr;
		m_Token = 0;

		if (fileSystem != nullptr)
		{
			fileSystem->releaseExclusiveFileLock(token);
		}
	}

	FileSystemOwner::~FileSystemOwner() noexcept = default;
	FileSystemOwner::FileSystemOwner(FileSystemOwner&& other) noexcept = default;
	FileSystemOwner& FileSystemOwner::operator=(FileSystemOwner&& other) noexcept = default;

	void FileSystemOwner::Reset() noexcept
	{
		m_Owner.Reset();
	}

	IFileSystem* FileSystemOwner::Get() const noexcept
	{
		return m_Owner.Get();
	}

	core::MemoryResource* FileSystemOwner::GetResource() const noexcept
	{
		return m_Owner.GetResource();
	}

	FileSystemOwner::operator bool() const noexcept
	{
		return static_cast<bool>(m_Owner);
	}

	IFileSystem* FileSystemOwner::operator->() const noexcept
	{
		return m_Owner.operator->();
	}
} // namespace rox::os
