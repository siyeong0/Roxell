#pragma once

#include <InterfaceOwner.hpp>
#include <WindowTextDocument.h>
#include <WindowTypes.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace rox::os
{
	class WindowsTextInput final
	{
	public:
		using Emit = void (*)(void*, const WindowEvent&) noexcept;

		WindowsTextInput(core::MemoryResource& memory, HWND window, WindowId id, Emit emit, void* context) noexcept;
		~WindowsTextInput() noexcept;

		WindowsTextInput(const WindowsTextInput&) = delete;
		WindowsTextInput& operator=(const WindowsTextInput&) = delete;

		core::Result<void> Initialize();
		core::Result<void> Publish(const WindowTextDocument& document);
		core::Result<void> SetState(const WindowTextInputState& state);

		bool FilterKey(const MSG& message) noexcept;
		void Pump() noexcept;
		void Focus(bool bFocused) noexcept;
		void Close() noexcept;

	private:
		class State;

	private:
		core::MemoryResource& m_Memory;
		HWND m_Window;
		WindowId m_Id;

		Emit m_Emit;
		void* m_Context;

		State* m_State = nullptr;
	};
}
