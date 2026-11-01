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
#if !defined(_WIN32)
	namespace
	{
		core::ThreadId identity(pthread_t thread) noexcept
		{
			static_assert(sizeof(thread) <= sizeof(core::uint64));
			core::ThreadId id{};
			core::CopyBytes(&id.Value, &thread, sizeof(thread));

			return id;
		}

		class PosixThreadBackend final : public core::IThreadBackend
		{
			struct Handle
			{
				pthread_t Thread{};
			};
			static void* run(void* context) noexcept
			{
				const core::NativeThreadEntry& entry = *static_cast<core::NativeThreadEntry*>(context);
				entry.Function(entry.Context);

				return nullptr;
			}

		public:
			core::NativeThreadStartResult Start(core::NativeThreadEntry& entry) noexcept override
			{
				ASSERT(entry.Function != nullptr, "Native thread requires an entry function.");
				core::SystemMemoryResource& memory = core::SystemMemoryResource::Get();
				Handle* handle = core::CreateObject<Handle>(memory);

				if (handle == nullptr)
				{
					return { {}, {core::EThreadStartStatus::NativeFailure, core::NativeOutOfMemory} };
				}

				const int error = pthread_create(&handle->Thread, nullptr, run, &entry);

				if (error != 0)
				{
					core::DestroyObject(memory, handle);

					return { {}, {core::EThreadStartStatus::NativeFailure, static_cast<core::uint32>(error)} };
				}

				return { {handle, identity(handle->Thread)}, {core::EThreadStartStatus::Started, 0} };
			}

			void Join(core::NativeThread thread) noexcept override
			{
				Handle* handle = static_cast<Handle*>(thread.Handle);
				ASSERT(handle != nullptr, "Native join requires owned thread storage.");
				const int error = pthread_join(handle->Thread, nullptr);
				ASSERT(error == 0, "Native join must observe complete termination.");
				core::DestroyObject(core::SystemMemoryResource::Get(), handle);
			}

			core::ThreadId CurrentThreadId() const noexcept override
			{
				return identity(pthread_self());
			}
		};
	} // namespace
#endif
	core::IThreadBackend& GetPlatformThreadBackend() noexcept
	{
#if defined(_WIN32)
		static WindowsThreadBackend backend;
#else
		static PosixThreadBackend backend;
#endif
		return backend;
	}
} // namespace rox::os
