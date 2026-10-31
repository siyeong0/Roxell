#pragma once

#include <SharedPtr.hpp>
#include <StringView.hpp>
#include <UniqueFunction.hpp>

namespace rox::core
{
	class JobScheduler;
	class JobState;
	template <typename> class Task;
	template <typename> class TaskSource;

	namespace detail
	{
		class TaskState;
		class TaskSignal;
	}

	// Copyable completion dependency; never owns the right to consume a result.
	// The allocating resource outlives every copy. Mutating one handle requires exclusive access.
	class TaskHandle final
	{
		friend class detail::TaskState;
		friend class JobState;
		friend class JobScheduler;
		template <typename> friend class Task;
		template <typename> friend class TaskSource;

	public:
		TaskHandle() noexcept;
		~TaskHandle() noexcept;
		TaskHandle(const TaskHandle& other) noexcept;
		TaskHandle& operator=(const TaskHandle& other) noexcept;
		TaskHandle(TaskHandle&& other) noexcept;
		TaskHandle& operator=(TaskHandle&& other) noexcept;
		void Reset() noexcept;

		bool IsValid() const noexcept;
		bool IsComplete() const noexcept;
		bool IsRetired() const noexcept;
		// Does not pump scheduler work. Join first when work needs the calling thread.
		void WaitForRetirement() const noexcept;
		bool WasSkipped() const noexcept;
		StringView GetName() const noexcept;

	private:
		explicit TaskHandle(JobState* state) noexcept;
		explicit TaskHandle(SharedPtr<detail::TaskSignal> signal) noexcept;
		static TaskHandle ready() noexcept;
		static TaskHandle createSignal(MemoryResource& resource) noexcept;
		static TaskHandle tryCreateSignal(MemoryResource& resource) noexcept;
		void observeCompletion(UniqueFunction<void() noexcept> callback) const noexcept;
		void disconnectCompletion() const noexcept;
		UniqueFunction<void() noexcept> observeSignal(MemoryResource& resource, UniqueFunction<void() noexcept> callback) const noexcept;

	private:
		JobState* m_State = nullptr;
		SharedPtr<detail::TaskSignal> m_Signal;
		bool m_bReady = false;
	};
}
