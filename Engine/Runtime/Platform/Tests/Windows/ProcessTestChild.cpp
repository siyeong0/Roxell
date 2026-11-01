#include <WindowsProcessService.h>
#include <WindowsExecutionEnvironment.h>
#include <CoreString.hpp>
#include <Hash.h>
#include <StringView.hpp>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <io.h>

int main(const int argc, char** const argv)
{
	(void)_setmode(_fileno(stdout), _O_BINARY);
	(void)_setmode(_fileno(stderr), _O_BINARY);
	if (argc < 2)
	{
		return EXIT_FAILURE;
	}
	const rox::core::StringView mode(argv[1]);
	if (mode == "echo")
	{
		for (int index = 2; index < argc; ++index)
		{
			const rox::core::StringView argument(argv[index]);
			std::printf("%zu:", argument.Size());
			std::fwrite(argument.Data(), 1, argument.Size(), stdout);
			std::fputc('\n', stdout);
		}
		std::fputs("stderr-line\n", stderr);
		return EXIT_SUCCESS;
	}
	if (mode == "environment-hash")
	{
		const rox::core::Result<rox::os::WindowsExecutionEnvironment> environment = rox::os::ReadWindowsExecutionEnvironment();
		if (!environment || !environment.Value().bDefaultFloatingPointMode)
		{
			return EXIT_FAILURE;
		}
		const rox::core::String text = rox::core::ToString(environment.Value().Fingerprint);
		std::fwrite(text.Data(), 1, text.Size(), stdout);
		return EXIT_SUCCESS;
	}
	if (mode == "image-hash")
	{
		const rox::core::Result<rox::core::Hash256> hash = rox::os::ReadCurrentWindowsProcessImageHash();
		if (!hash)
		{
			return EXIT_FAILURE;
		}
		const rox::core::String text = rox::core::ToString(hash.Value());
		std::fwrite(text.Data(), 1, text.Size(), stdout);
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}
