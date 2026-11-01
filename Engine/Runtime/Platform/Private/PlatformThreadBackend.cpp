#include <PlatformThreadBackend.h>

#if defined(_WIN32)
#include <WindowsThreadBackend.h>
#else
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <SystemMemoryResource.h>
#include <NativeRuntime.hpp>
#include <UniquePtr.hpp>
#include <pthread.h>
#endif

namespace rox::os
{
	using namespace core;

#if !defined(_WIN32)
	namespace
	{
		ThreadId identity(pthread_t thread) noexcept
		{
			static_assert(sizeof(thread) <= sizeof(uint64));
			ThreadId id{};
			CopyBytes(&id.Value, &thread, sizeof(thread));

			return id;
		}

		class PosixThreadBackend final : public IThreadBackend
		{
			struct Handle
			{
				pthread_t Thread{};
			};
			static void* run(void* context) noexcept
			{
				const NativeThreadEntry& entry = *static_cast<NativeThreadEntry*>(context);
				entry.Function(entry.Context);

				return nullptr;
			}

		public:
			NativeThreadStartResult Start(NativeThreadEntry& entry) noexcept override
			{
				ASSERT(entry.Function != nullptr, "Native thread requires an entry function.");
				SystemMemoryResource& memory = SystemMemoryResource::Get();
				Handle* handle = CreateObject<Handle>(memory);

				if (handle == nullptr)
				{
					return { {}, {EThreadStartStatus::NativeFailure, NativeOutOfMemory} };
				}

				const int error = pthread_create(&handle->Thread, nullptr, run, &entry);

				if (error != 0)
				{
					DestroyObject(memory, handle);

					return { {}, {EThreadStartStatus::NativeFailure, static_cast<uint32>(error)} };
				}

				return { {handle, identity(handle->Thread)}, {EThreadStartStatus::Started, 0} };
			}

			void Join(NativeThread thread) noexcept override
			{
				Handle* handle = static_cast<Handle*>(thread.Handle);
				ASSERT(handle != nullptr, "Native join requires owned thread storage.");
				const int error = pthread_join(handle->Thread, nullptr);
				ASSERT(error == 0, "Native join must observe complete termination.");
				DestroyObject(SystemMemoryResource::Get(), handle);
			}

			ThreadId CurrentThreadId() const noexcept override
			{
				return identity(pthread_self());
			}
		};
	} // namespace
#endif
	IThreadBackend& GetPlatformThreadBackend() noexcept
	{
#if defined(_WIN32)
		static WindowsThreadBackend backend;
#else
		static PosixThreadBackend backend;
#endif
		return backend;
	}
} // namespace rox::os
