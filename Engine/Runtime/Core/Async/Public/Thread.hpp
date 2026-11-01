#pragma once

#include <IThreadBackend.h>
#include <TypeVocabulary.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <UniqueFunction.hpp>

#include <type_traits>
#include <utility>

namespace rox::core
{
	// The resource and backend outlive Join. The owner requires confinement or external synchronization.
	class Thread final
	{
	public:
		Thread(MemoryResource& resource, IThreadBackend& backend) noexcept;

		// Destruction requires explicit Join; it never blocks or detaches implicitly.
		~Thread() noexcept;
		Thread(const Thread&) = delete;
		Thread& operator=(const Thread&) = delete;
		Thread(Thread&& other) noexcept;
		// The destination must be empty. Moving transfers resources without moving the running callable.
		Thread& operator=(Thread&& other) noexcept;

		// Consumes the target on success or native refusal. Mandatory allocation uses the resource policy.
		[[nodiscard]] ThreadStartResult Start(UniqueFunction<void() noexcept>&& target) noexcept;

		template <typename TTarget>
		[[nodiscard]] ThreadStartResult Start(TTarget&& target) noexcept
		    requires(!IsSame<core::Decay<TTarget>, UniqueFunction<void() noexcept>> && IsConstructible<UniqueFunction<void() noexcept>, MemoryResource&, TTarget&&>)
		{
			ASSERT(!IsJoinable(), "Thread start requires empty ownership.");
			return Start(UniqueFunction<void() noexcept>(*m_Resource, core::Forward<TTarget>(target)));
		}

		// Waits for native termination before destroying the callable on the joining thread.
		void Join() noexcept;

		// Completion alone does not release joinable ownership. This is not a concurrent completion query.
		[[nodiscard]] bool IsJoinable() const noexcept;
		[[nodiscard]] ThreadId GetId() const noexcept;

	private:
		struct Control;

		static void execute(void* context) noexcept;
		void moveFrom(Thread& other) noexcept;

	private:
		MemoryResource* m_Resource;
		IThreadBackend* m_Backend;
		Control* m_Control = nullptr;
		NativeThread m_Native;
	};
}
