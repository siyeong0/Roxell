#include <Thread.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <UniquePtr.hpp>

namespace rox::core
{
	struct Thread::Control final
	{
		explicit Control(UniqueFunction<void() noexcept>&& target) noexcept
		    : Target(core::Move(target))
		    , Entry{execute, this}
		{
		}

		UniqueFunction<void() noexcept> Target;
		NativeThreadEntry Entry;
	};

	Thread::Thread(MemoryResource& resource, IThreadBackend& backend) noexcept
	    : m_Resource(&resource)
	    , m_Backend(&backend)
	{
	}

	Thread::~Thread() noexcept
	{
		ASSERT(!IsJoinable(), "Thread must be joined before destruction.");
	}

	Thread::Thread(Thread&& other) noexcept
	{
		moveFrom(other);
	}

	Thread& Thread::operator=(Thread&& other) noexcept
	{
		ASSERT(this == &other || !IsJoinable(), "Thread move assignment requires empty destination ownership.");

		if (this != &other)
		{
			moveFrom(other);
		}

		return *this;
	}

	ThreadStartResult Thread::Start(UniqueFunction<void() noexcept>&& target) noexcept
	{
		ASSERT(!IsJoinable(), "Thread start requires empty ownership.");
		ASSERT(static_cast<bool>(target), "Thread start requires a callable target.");

		Control* const control = CreateObjectRequired<Control>(*m_Resource, core::Move(target));
		const NativeThreadStartResult started = m_Backend->Start(control->Entry);

		if (started.Result.Status == EThreadStartStatus::Started)
		{
			ASSERT(started.Thread.Handle != nullptr && started.Thread.Id.Value != 0 && started.Result.NativeError == 0, "Thread backend must return valid started ownership.");

			m_Control = control;
			m_Native = started.Thread;
		}
		else
		{
			ASSERT(started.Result.Status == EThreadStartStatus::NativeFailure && started.Thread.Handle == nullptr && started.Thread.Id.Value == 0, "Thread backend refusal must publish no owned execution.");
			DestroyObject(*m_Resource, control);
		}

		return started.Result;
	}

	void Thread::Join() noexcept
	{
		ASSERT(IsJoinable(), "Thread join requires owned execution.");

		m_Backend->Join(m_Native);
		DestroyObject(*m_Resource, m_Control);
		m_Control = nullptr;
		m_Native = {};
	}

	bool Thread::IsJoinable() const noexcept
	{
		return m_Control != nullptr;
	}

	ThreadId Thread::GetId() const noexcept
	{
		return m_Native.Id;
	}

	void Thread::execute(void* context) noexcept
	{
		static_cast<Control*>(context)->Target();
	}

	void Thread::moveFrom(Thread& other) noexcept
	{
		m_Resource = other.m_Resource;
		m_Backend = other.m_Backend;
		m_Control = core::Exchange(other.m_Control, nullptr);
		m_Native = core::Exchange(other.m_Native, NativeThread{});
	}
}
