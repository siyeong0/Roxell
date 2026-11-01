#pragma once

#include <Assertion.h>
#include <MemoryResource.h>
#include <SharedPtr.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace rox::core
{
	class CancellationRegistration;

	namespace detail
	{
		struct CancellationNode;

		enum class ECancellationDispatchPhase : uint8
		{
			Open,
			Dispatching,
			Complete
		};

		struct CancellationState final
		{
			Atomic<ECancellationDispatchPhase> DispatchPhase = ECancellationDispatchPhase::Open;
			Atomic<uint64> RegistrationChangeSequence = 0;
			Mutex Mutex;
			CancellationNode* RegistrationHead = nullptr;
		};

	}

	class CancellationToken
	{
		friend class CancellationRegistration;
		friend class CancellationSource;

	public:
		CancellationToken() noexcept = default;

		[[nodiscard]] bool CanBeCancelled() const noexcept;
		[[nodiscard]] bool IsCancellationRequested() const noexcept;

	private:
		explicit CancellationToken(SharedPtr<detail::CancellationState> state) noexcept;

	private:
		SharedPtr<detail::CancellationState> m_State;
	};
}

namespace rox::core
{
	using CancellationCallback = void (*)(void* context) noexcept;

	enum class ECancellationRegistrationResult
	{
		NotCancellable,
		Registered,
		InvokedSynchronously
	};

	namespace detail
	{
		enum class ECancellationNodePhase : uint8
		{
			Idle,
			Registered,
			Invoking,
			Completed
		};

		struct CancellationNode final
		{
			void Unlink(CancellationState& state) noexcept;

			CancellationRegistration* Owner;
			CancellationCallback Callback = nullptr;
			void* Context = nullptr;
			CancellationNode* Previous = nullptr;
			CancellationNode* Next = nullptr;
			Atomic<ECancellationNodePhase> Phase = ECancellationNodePhase::Idle;
		};
	}

	// Registration is an allocation-free intrusive node. Its address must remain stable while registered.
	// Register and Reset calls on the same object require external synchronization. Outside callback context,
	// Reset waits for an already-running callback. Callback code must not destroy or reset its own registration
	// or wait on another in-flight registration. The borrowed context must remain valid until Reset returns or
	// a terminal callback has returned.
	class CancellationRegistration final
	{
	public:
		CancellationRegistration() noexcept = default;
		~CancellationRegistration();
		CancellationRegistration(const CancellationRegistration&) = delete;
		CancellationRegistration& operator=(const CancellationRegistration&) = delete;
		CancellationRegistration(CancellationRegistration&&) = delete;
		CancellationRegistration& operator=(CancellationRegistration&&) = delete;

		[[nodiscard]] ECancellationRegistrationResult Register(const CancellationToken& token,
		                                                       CancellationCallback callback, void* context) noexcept;
		void Reset() noexcept;

	private:
		void acquireOperation() noexcept;
		void releaseOperation() noexcept;
		void resetInternal() noexcept;

	private:
		SharedPtr<detail::CancellationState> m_State;
		detail::CancellationNode m_Node{this};

		Atomic<bool> m_bOperationActive = false;
	};
}

namespace rox::core
{
	class CancellationRegistration;

	class CancellationSource
	{
	public:
		// Requires a thread-safe resource; allocation failure returns an invalid source.
		[[nodiscard]] static CancellationSource Create(MemoryResource& resource) noexcept;

		CancellationSource() noexcept;
		explicit CancellationSource(MemoryResource& resource) noexcept;
		CancellationSource(const CancellationSource&) = delete;
		CancellationSource& operator=(const CancellationSource&) = delete;
		CancellationSource(CancellationSource&&) noexcept = default;
		CancellationSource& operator=(CancellationSource&&) noexcept = default;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] CancellationToken GetToken() const noexcept;
		// Invokes callbacks synchronously and may wait for another thread's dispatch. Do not hold
		// locks needed by callbacks. Repeated requests are harmless; this does not join cancelled work.
		void RequestCancellation() noexcept;

	private:
		struct EmptyTag final
		{
		};

		explicit CancellationSource(EmptyTag) noexcept;

	private:
		SharedPtr<detail::CancellationState> m_State;
	};
}
