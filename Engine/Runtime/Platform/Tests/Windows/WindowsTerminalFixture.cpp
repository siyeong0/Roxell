#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <string>

namespace
{
	bool writeText(const char* text, DWORD length)
	{
		DWORD written = 0;
		return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, nullptr) && written == length;
	}
}

int main()
{
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
	if (!SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT) || !writeText("READY\r\n", 7))
	{
		return 90;
	}

	std::string command;
	for (;;)
	{
		char value = 0;
		DWORD count = 0;
		if (!ReadFile(input, &value, 1, &count, nullptr) || count != 1)
		{
			return 91;
		}
		if (value != '\r')
		{
			command += value;
			continue;
		}
		if (command == "size")
		{
			CONSOLE_SCREEN_BUFFER_INFO info{};
			if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
			{
				return 92;
			}
			char result[80]{};
			const int length = std::snprintf(result, sizeof(result), "SIZE:%d:%d\r\n",
				info.srWindow.Right - info.srWindow.Left + 1, info.srWindow.Bottom - info.srWindow.Top + 1);
			if (length <= 0 || !writeText(result, static_cast<DWORD>(length)))
			{
				return 93;
			}
		}
		else if (command == "flood")
		{
			if (!writeText("FLOODING\r\n", 10))
			{
				return 95;
			}
			char buffer[4096];
			for (char& byte : buffer)
			{
				byte = 'z';
			}
			for (int index = 0; index < 1024; ++index)
			{
				if (!writeText(buffer, sizeof(buffer)))
				{
					return 96;
				}
			}
		}
		else if (command == "burst")
		{
			if (!writeText("BURST-START\r\n", 13))
			{
				return 97;
			}
			char buffer[4096];
			for (char& byte : buffer)
			{
				byte = 'q';
			}
			for (int index = 0; index < 16; ++index)
			{
				if (!writeText(buffer, sizeof(buffer)))
				{
					return 98;
				}
			}
			if (!writeText("BURST-END\r\n", 11))
			{
				return 99;
			}
		}
		else if (command == "exit")
		{
			return 37;
		}
		else if (!writeText("ECHO:", 5) || !writeText(command.data(), static_cast<DWORD>(command.size())) || !writeText("\r\n", 2))
		{
			return 94;
		}
		command.clear();
	}
}
