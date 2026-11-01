extern "C" __declspec(dllimport) int PlatformTestDependencyValue();

extern "C" __declspec(dllexport) int PlatformTestDependentValue()
{
	return PlatformTestDependencyValue();
}
