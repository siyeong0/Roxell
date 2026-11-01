#include <FileSystem.h>
#include <SystemMemoryResource.h>

namespace rox::os
{
	using namespace core;

	Result<Array<Byte>> IFileSystem::ReadFile(StringView relativePath, uint64 maximumBytes) const
	{
		return ReadFile(relativePath, maximumBytes, SystemMemoryResource::Get());
	}

	Result<ExclusiveFileLock> ExclusiveFileLock::Acquire(IFileSystem& fileSystem, const StringView relativePath)
	{
		Result<uintptr> tokenResult = fileSystem.tryAcquireExclusiveFileLock(relativePath);

		if (!tokenResult)
		{
			return Result<ExclusiveFileLock>::Failure(tokenResult.GetError());
		}

		return Result<ExclusiveFileLock>::Success(ExclusiveFileLock(fileSystem, tokenResult.Value()));
	}

	ExclusiveFileLock::ExclusiveFileLock(IFileSystem& fileSystem, const uintptr token) noexcept
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
		const uintptr token = m_Token;
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

	MemoryResource* FileSystemOwner::GetResource() const noexcept
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
