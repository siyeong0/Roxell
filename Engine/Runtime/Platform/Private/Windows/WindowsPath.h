#pragma once

#include <Array.hpp>
#include <Result.hpp>
#include <TextPrimitives.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Types.h>

namespace rox::os
{
	// Explicit scratch ownership avoids allocating standard-library debug iterator proxies during moves.
	class WindowsPath final
	{
	public:
		explicit WindowsPath(core::MemoryResource& resource) noexcept
			: m_Text(resource)
		{
		}

		WindowsPath(WindowsPath&&) noexcept = default;
		WindowsPath& operator=(WindowsPath&& other) noexcept;
		WindowsPath(const WindowsPath&) = delete;
		WindowsPath& operator=(const WindowsPath&) = delete;

		[[nodiscard]] core::MemoryResource& Resource() const noexcept { return m_Text.GetResource(); }
		[[nodiscard]] const wchar_t* CStr() const noexcept { return m_Text.IsEmpty() ? L"" : m_Text.Data(); }
		[[nodiscard]] wchar_t* Data() noexcept { return m_Text.Data(); }
		[[nodiscard]] rox::core::WideStringView View() const noexcept { return { CStr(), Size() }; }
		[[nodiscard]] rox::core::usize Size() const noexcept { return m_Text.IsEmpty() ? 0 : m_Text.Size() - 1; }
		[[nodiscard]] bool IsEmpty() const noexcept { return Size() == 0; }

		[[nodiscard]] bool Resize(rox::core::usize size);
		[[nodiscard]] bool Assign(rox::core::WideStringView text);
		[[nodiscard]] bool Append(rox::core::WideStringView text);
		[[nodiscard]] bool AppendComponent(rox::core::WideStringView text);
		[[nodiscard]] core::Result<WindowsPath> Parent() const;

	private:
		core::Array<wchar_t> m_Text;
	};

	[[nodiscard]] bool IsPathSeparator(wchar_t character) noexcept;
	[[nodiscard]] bool IsMissingPathError(const rox::core::SystemErrorCode& error) noexcept;
	[[nodiscard]] rox::core::usize RootNameSize(rox::core::WideStringView path) noexcept;
	[[nodiscard]] rox::core::usize RootPathSize(rox::core::WideStringView path) noexcept;
	[[nodiscard]] bool IsAbsolutePath(rox::core::WideStringView path) noexcept;
	[[nodiscard]] bool IsWithinRoot(const WindowsPath& root, const WindowsPath& candidate, bool bWindowsCaseComparison = false) noexcept;
	[[nodiscard]] core::Result<WindowsPath> PathFromUtf8(rox::core::StringView text, core::MemoryResource& resource);
	[[nodiscard]] core::Result<WindowsPath> AbsolutePath(const WindowsPath& input, rox::core::SystemErrorCode& error);
	[[nodiscard]] core::Result<WindowsPath> WeaklyCanonicalPath(const WindowsPath& input, rox::core::SystemErrorCode& error);
	[[nodiscard]] core::Result<void> CreatePathDirectories(const WindowsPath& path, rox::core::SystemErrorCode& error);
	[[nodiscard]] unsigned long PathAttributes(const WindowsPath& path, rox::core::SystemErrorCode& error) noexcept;
	[[nodiscard]] core::uint64 PathFileSize(const WindowsPath& path, rox::core::SystemErrorCode& error) noexcept;
}
