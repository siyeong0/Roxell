#include "ShaderTestSources.h"
#include "FailingThreadBackend.h"
#include "GatedShaderCompiler.h"
#include "ShaderCompilerGate.h"

#include <FailingMemoryResource.h>
#include <NumericLimits.hpp>
#include <ScopeExit.hpp>
#include <ShaderBuilder.h>
#include <ShaderBuildService.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <WindowsThreadBackend.h>

#include <chrono>
#include <thread>

#include "ShaderCompilerTest.h"

namespace
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	using namespace std::chrono_literals;

	bool prepare(
		core::MemoryResource& memory,
		shader::ShaderBuildPlan& plan,
		core::StringView offset = "static const uint Offset = 7;\n",
		core::uint32 diagnosticLimit = 128,
		core::uint32 abi = shader::SHADER_ABI_VERSION)
	{
		shader::ShaderCompilerOptions options;
		options.ShaderAbiVersion = abi;
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, options, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		CHECK(test::MakeComputeRequest(request, memory, shader::EShaderTarget::SpirV));
		request.Limits.MaximumDiagnostics = diagnosticLimit;
		test::MemoryShaderSources sources;
		sources.OffsetSource = offset;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildPlan> prepared = compiler.Value()->PrepareBuild(request, sources, report, memory);
		CHECK(test::Succeeded(prepared));
		plan = std::move(prepared).TakeValue();

		return true;
	}

	shader::ShaderBuildServiceDesc settings(os::WindowsThreadBackend& threads, test::ShaderCompilerGate& gate)
	{
		shader::ShaderBuildServiceDesc desc;
		desc.ThreadBackend = &threads;
		desc.CompilerFactory = &test::GatedShaderCompiler::Create;
		desc.CompilerFactoryContext = &gate;
		desc.WorkerCount = 1;
		desc.MaximumQueuedJobs = 2;
		desc.MaximumSubscribers = 8;
		desc.MaximumResultBytesPerJob = 4u * 1024u * 1024u;
		desc.MaximumRetainedResultBytes = 16u * 1024u * 1024u;

		return desc;
	}

	bool waitForTerminal(const shader::ShaderBuildJob& job, shader::ShaderBuildJobSnapshot& result)
	{
		const core::MonotonicClock::time_point deadline = std::chrono::steady_clock::now() + 5s;
		do
		{
			result = job.Poll();
			if (result.State != shader::EShaderBuildJobState::Queued && result.State != shader::EShaderBuildJobState::Running)
			{
				return true;
			}

			std::this_thread::sleep_for(1ms);
		} while (std::chrono::steady_clock::now() < deadline);

		return false;
	}

	bool waitForResultRelease(const shader::ShaderBuildService& service)
	{
		const core::MonotonicClock::time_point deadline = std::chrono::steady_clock::now() + 5s;
		do
		{
			if (service.GetStatistics().RetainedResultBytes == 0)
			{
				return true;
			}

			std::this_thread::sleep_for(1ms);
		} while (std::chrono::steady_clock::now() < deadline);

		return false;
	}

	bool testDedupCancellationAndDrain(core::MemoryResource& memory)
	{
		shader::ShaderBuildPlan plan;
		CHECK(prepare(memory, plan));
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		core::InterfaceOwner<shader::ShaderBuildService> createdOwner = core::InterfaceOwner<shader::ShaderBuildService>::Create(memory, memory);
		CHECK(createdOwner);
		core::Result<void> created = createdOwner->Start(settings(threads, gate));
		CHECK(test::Succeeded(created));
		core::InterfaceOwner<shader::ShaderBuildService> service = core::Move(createdOwner);
		core::Result<ShaderBuildJob> first = service->Submit(plan);
		CHECK(test::Succeeded(first));
		CHECK(gate.WaitForEntries(1, 5s));
		core::Result<ShaderBuildJob> second = service->Submit(plan);
		CHECK(test::Succeeded(second));
		shader::ShaderBuildJob copy = second.Value();
		first.Value().Cancel();
		CHECK(first.Value().Poll().State == shader::EShaderBuildJobState::Cancelled);
		CHECK(second.Value().Poll().State == shader::EShaderBuildJobState::Running);
		const ShaderBuildStatistics running = service->GetStatistics();
		CHECK(running.CompilerExecutions == 1 && running.DeduplicatedSubscribers == 1);
		core::Result<void> timeout = service->Drain(1ms);
		CHECK(!timeout && timeout.GetError().Code == core::EErrorCode::Timeout);
		CHECK(!service->Submit(plan));
		gate.Open();
		CHECK(test::Succeeded(service->Drain(5s)));
		ShaderBuildJobSnapshot completed = second.Value().Poll();
		CHECK(completed.State == shader::EShaderBuildJobState::Succeeded && completed.Result.HasOutput());
		CHECK(copy.Poll().Result.GetOutput().Artifact.GetIdentity() == completed.Result.GetOutput().Artifact.GetIdentity());
		second.Value().Cancel();
		CHECK(second.Value().Poll().State == shader::EShaderBuildJobState::Succeeded);
		CHECK(first.Value().Poll().State == shader::EShaderBuildJobState::Cancelled);
		CHECK(service->GetStatistics().bDrained && service->GetStatistics().AdmittedSourceBytes == 0);
		shader::Shader artifact = completed.Result.GetOutput().Artifact;
		first.Value() = {};
		second.Value() = {};
		copy = {};
		completed = {};
		CHECK(service->GetStatistics().RetainedResultBytes > 0);
		service.Reset();
		core::Result<core::Array<core::Byte>> encoded = artifact.Save(memory);
		CHECK(test::Succeeded(encoded));
		CHECK(artifact.GetIdentity().IsValid());

		return true;
	}

	bool testDiagnosticPolicyAndBoundedQueue(core::MemoryResource& memory)
	{
		shader::ShaderBuildPlan firstPlan;
		shader::ShaderBuildPlan moreDiagnostics;
		shader::ShaderBuildPlan differentSource;
		CHECK(prepare(memory, firstPlan, "static const uint Offset = 7;\n", 1));
		CHECK(prepare(memory, moreDiagnostics, "static const uint Offset = 7;\n", 128));
		CHECK(prepare(memory, differentSource, "static const uint Offset = 9;\n", 128));
		CHECK(firstPlan.GetBuildIdentity() == moreDiagnostics.GetBuildIdentity());
		CHECK(firstPlan.GetBuildIdentity() != differentSource.GetBuildIdentity());
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		shader::ShaderBuildServiceDesc desc = settings(threads, gate);
		desc.MaximumQueuedJobs = 1;
		core::InterfaceOwner<shader::ShaderBuildService> createdOwner = core::InterfaceOwner<shader::ShaderBuildService>::Create(memory, memory);
		CHECK(createdOwner);
		core::Result<void> created = createdOwner->Start(desc);
		CHECK(test::Succeeded(created));
		core::InterfaceOwner<shader::ShaderBuildService> service = core::Move(createdOwner);
		core::Result<ShaderBuildJob> first = service->Submit(firstPlan);
		CHECK(first && gate.WaitForEntries(1, 5s));
		core::Result<ShaderBuildJob> second = service->Submit(moreDiagnostics);
		CHECK(test::Succeeded(second));
		CHECK(second.Value().Poll().State == shader::EShaderBuildJobState::Queued);
		CHECK(service->GetStatistics().DeduplicatedSubscribers == 0);
		CHECK(!service->Submit(differentSource));
		core::Result<void> timeout = service->CancelAndDrain(1ms);
		CHECK(!timeout && timeout.GetError().Code == core::EErrorCode::Timeout);
		CHECK(first.Value().Poll().State == shader::EShaderBuildJobState::Cancelled);
		CHECK(second.Value().Poll().State == shader::EShaderBuildJobState::Cancelled);
		gate.Open();
		CHECK(test::Succeeded(service->CancelAndDrain(5s)));
		CHECK(gate.GetEntryCount() == 1);

		return true;
	}

	bool testDeadlineWhileCompilerBlocked(core::MemoryResource& memory)
	{
		shader::ShaderBuildPlan plan;
		CHECK(prepare(memory, plan));
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		core::InterfaceOwner<shader::ShaderBuildService> serviceOwner = core::InterfaceOwner<shader::ShaderBuildService>::Create(memory, memory);
		CHECK(serviceOwner);
		core::Result<void> service = serviceOwner->Start(settings(threads, gate));
		CHECK(test::Succeeded(service));
		core::Result<ShaderBuildJob> first = serviceOwner->Submit(plan);
		CHECK(first && gate.WaitForEntries(1, 5s));
		shader::ShaderBuildJobOptions options;
		options.bHasDeadline = true;
		options.Deadline = std::chrono::steady_clock::now() + 100ms;
		core::Result<ShaderBuildJob> timed = serviceOwner->Submit(plan, options);
		CHECK(test::Succeeded(timed));
		const core::MonotonicClock::time_point deadline = std::chrono::steady_clock::now() + 5s;
		while (serviceOwner->GetStatistics().CancelledRunningSubscribers == 0 && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(1ms);
		}

		CHECK(serviceOwner->GetStatistics().CancelledRunningSubscribers == 1);
		const ShaderBuildJobSnapshot terminal = timed.Value().Poll();
		CHECK(terminal.State == shader::EShaderBuildJobState::Cancelled);
		CHECK(first.Value().Poll().State == shader::EShaderBuildJobState::Running);
		gate.Open();
		CHECK(test::Succeeded(serviceOwner->Drain(5s)));
		CHECK(first.Value().Poll().State == shader::EShaderBuildJobState::Succeeded);
		CHECK(gate.GetEntryCount() == 1);

		return true;
	}

	bool testRetainedResultBudget(core::MemoryResource& memory)
	{
		shader::ShaderBuildPlan firstPlan;
		shader::ShaderBuildPlan secondPlan;
		CHECK(prepare(memory, firstPlan));
		CHECK(prepare(memory, secondPlan, "static const uint Offset = 11;\n"));
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		gate.Open();
		shader::ShaderBuildServiceDesc desc = settings(threads, gate);
		desc.MaximumRetainedResultBytes = desc.MaximumResultBytesPerJob;
		core::InterfaceOwner<shader::ShaderBuildService> serviceOwner = core::InterfaceOwner<shader::ShaderBuildService>::Create(memory, memory);
		CHECK(serviceOwner);
		core::Result<void> service = serviceOwner->Start(desc);
		CHECK(test::Succeeded(service));
		core::Result<ShaderBuildJob> first = serviceOwner->Submit(firstPlan);
		CHECK(test::Succeeded(first));
		shader::ShaderBuildJobSnapshot terminal;
		CHECK(waitForTerminal(first.Value(), terminal));
		CHECK(terminal.State == shader::EShaderBuildJobState::Succeeded);
		shader::Shader artifact = terminal.Result.GetOutput().Artifact;
		first.Value() = {};
		terminal = {};
		CHECK(serviceOwner->GetStatistics().RetainedResultBytes > 0);
		CHECK(!serviceOwner->Submit(secondPlan));
		artifact = {};
		CHECK(waitForResultRelease(*serviceOwner));
		core::Result<ShaderBuildJob> second = serviceOwner->Submit(secondPlan);
		CHECK(test::Succeeded(second));
		CHECK(test::Succeeded(serviceOwner->Drain(5s)));
		CHECK(second.Value().Poll().State == shader::EShaderBuildJobState::Succeeded);

		return true;
	}

	bool testSubscriberDetachAndToolchain(core::MemoryResource& memory)
	{
		shader::ShaderBuildPlan plan;
		CHECK(prepare(memory, plan));
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		shader::ShaderBuildServiceDesc desc = settings(threads, gate);
		desc.MaximumSubscribers = 1;
		core::InterfaceOwner<shader::ShaderBuildService> serviceOwner = core::InterfaceOwner<shader::ShaderBuildService>::Create(memory, memory);
		CHECK(serviceOwner);
		core::Result<void> service = serviceOwner->Start(desc);
		CHECK(test::Succeeded(service));
		core::Result<ShaderBuildJob> first = serviceOwner->Submit(plan);
		CHECK(first && gate.WaitForEntries(1, 5s));
		shader::ShaderBuildJob copy = first.Value();
		first.Value() = {};
		CHECK(!serviceOwner->Submit(plan));
		copy = {};
		core::Result<ShaderBuildJob> fresh = serviceOwner->Submit(plan);
		CHECK(test::Succeeded(fresh));
		CHECK(fresh.Value().Poll().State == shader::EShaderBuildJobState::Queued);
		gate.Open();
		CHECK(test::Succeeded(serviceOwner->Drain(5s)));
		CHECK(fresh.Value().Poll().State == shader::EShaderBuildJobState::Succeeded);
		CHECK(gate.GetEntryCount() == 2);
		desc.Compiler = shader::EShaderCompiler::Dxc;
		core::InterfaceOwner<shader::ShaderBuildService> otherOwner = core::InterfaceOwner<shader::ShaderBuildService>::Create(memory, memory);
		CHECK(otherOwner);
		core::Result<void> other = otherOwner->Start(desc);
		CHECK(test::Succeeded(other));
		CHECK(!otherOwner->Submit(plan));
		CHECK(test::Succeeded(otherOwner->Drain(5s)));

		return true;
	}

	bool testStartupRollback(core::MemoryResource& memory)
	{
		os::WindowsThreadBackend native;
		test::ShaderCompilerGate gate;

		for (core::uint32 failure = 1; failure <= 3; ++failure)
		{
			test::FailingThreadBackend threads(native, failure);
			shader::ShaderBuildServiceDesc desc = settings(native, gate);
			desc.WorkerCount = 2;
			desc.ThreadBackend = &threads;
			core::InterfaceOwner<shader::ShaderBuildService> serviceOwner = core::InterfaceOwner<shader::ShaderBuildService>::Create(memory, memory);
			CHECK(serviceOwner);
			core::Result<void> service = serviceOwner->Start(desc);
			CHECK(!service);
			CHECK(threads.GetJoinedCount() == failure - 1);
		}

		return true;
	}

	bool testCompilerOptions(core::MemoryResource& memory)
	{
		shader::ShaderBuildPlan first;
		shader::ShaderBuildPlan current;
		CHECK(prepare(memory, first));
		CHECK(prepare(memory, current, "static const uint Offset = 7;\n", 128, shader::SHADER_ABI_VERSION));
		CHECK(first.GetBuildIdentity() == current.GetBuildIdentity());
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		gate.Open();
		shader::ShaderBuildServiceDesc desc = settings(threads, gate);
		desc.CompilerOptions.ShaderAbiVersion = 2;
		{
			shader::ShaderBuildService invalid(memory);
			CHECK(!invalid.Start(desc));
		}

		desc.CompilerOptions.ShaderAbiVersion = shader::SHADER_ABI_VERSION;
		core::InterfaceOwner<shader::ShaderBuildService> serviceOwner = core::InterfaceOwner<shader::ShaderBuildService>::Create(memory, memory);
		CHECK(serviceOwner);
		core::Result<void> service = serviceOwner->Start(desc);
		CHECK(test::Succeeded(service));
		core::Result<ShaderBuildJob> job = serviceOwner->Submit(current);
		CHECK(test::Succeeded(job));
		CHECK(test::Succeeded(serviceOwner->Drain(5s)));
		ShaderBuildJobSnapshot result = job.Value().Poll();
		CHECK(result.State == shader::EShaderBuildJobState::Succeeded);
		CHECK(result.Result.GetOutput().Artifact.GetAbiVersion() == shader::SHADER_ABI_VERSION);

		return true;
	}

} // namespace

TEST(ShaderBuildService, DedupCancellationAndDrain)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDedupCancellationAndDrain(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, DiagnosticPolicyAndBoundedQueue)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDiagnosticPolicyAndBoundedQueue(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, DeadlineWhileCompilerBlocked)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDeadlineWhileCompilerBlocked(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, RetainedResultBudget)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testRetainedResultBudget(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, SubscriberDetachAndToolchain)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testSubscriberDetachAndToolchain(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, StartupRollback)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testStartupRollback(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, CompilerOptions)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testCompilerOptions(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, StoppedServiceIsSafeWithoutStartup)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
	{
		shader::ShaderBuildService service(memory);
		EXPECT_TRUE(service.GetStatistics().bDrained);
		EXPECT_TRUE(service.GetStatistics().bAdmissionClosed);
		EXPECT_TRUE(service.Drain(core::Milliseconds(0)));
		EXPECT_TRUE(service.CancelAndDrain(core::Milliseconds(0)));
		const core::Result<shader::ShaderBuildJob> submitted = service.Submit({});
		ASSERT_FALSE(submitted);
		EXPECT_EQ(submitted.GetError().Code, core::EErrorCode::Conflict);
	}

	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, FailedStartupCanRetryOnTheSameObject)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
	{
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		gate.Open();
		shader::ShaderBuildService service(memory);
		shader::ShaderBuildServiceDesc desc = settings(threads, gate);
		desc.WorkerCount = 0;
		EXPECT_FALSE(service.Start(desc));
		EXPECT_TRUE(service.GetStatistics().bDrained);
		desc.WorkerCount = 1;
		ASSERT_TRUE(service.Start(desc));
		EXPECT_TRUE(service.Drain(core::Milliseconds(5000)));
	}

	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, RestartPreservesOldJobsAndResults)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
	{
		shader::ShaderBuildPlan plan;
		ASSERT_TRUE(prepare(memory, plan));
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		gate.Open();
		shader::ShaderBuildService service(memory);
		const shader::ShaderBuildServiceDesc desc = settings(threads, gate);
		ASSERT_TRUE(service.Start(desc));
		core::Result<shader::ShaderBuildJob> first = service.Submit(plan);
		ASSERT_TRUE(first);
		ASSERT_TRUE(service.Drain(core::Milliseconds(5000)));
		shader::ShaderBuildJobSnapshot retained = first.Value().Poll();
		ASSERT_TRUE(retained.Result.HasOutput());
		const core::Hash256 identity = retained.Result.GetOutput().Artifact.GetIdentity();

		ASSERT_TRUE(service.Start(desc));
		core::Result<shader::ShaderBuildJob> second = service.Submit(plan);
		ASSERT_TRUE(second);
		ASSERT_TRUE(service.Drain(core::Milliseconds(5000)));
		EXPECT_TRUE(second.Value().Poll().Result.HasOutput());
		first.Value().Cancel();
		EXPECT_EQ(first.Value().Poll().State, shader::EShaderBuildJobState::Succeeded);
		EXPECT_EQ(retained.Result.GetOutput().Artifact.GetIdentity(), identity);
	}

	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, SubmissionAllocationFailuresRestoreAdmissionAndBudget)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		FailingMemoryResource memory(tracking, NumericLimits<usize>::max());
		shader::ShaderBuildPlan blockerPlan;
		shader::ShaderBuildPlan queuedPlan;
		ASSERT_TRUE(prepare(tracking, blockerPlan));
		ASSERT_TRUE(prepare(tracking, queuedPlan, "static const uint Offset = 19;\n"));
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		shader::ShaderBuildService service(memory);
		ASSERT_TRUE(service.Start(settings(threads, gate)));
		ScopeExit cleanup(
			[&]() noexcept
			{
				memory.Reset(NumericLimits<usize>::max());
				gate.Open();
				EXPECT_TRUE(service.CancelAndDrain(Milliseconds(5000)));
			});
		Result<shader::ShaderBuildJob> blocker = service.Submit(blockerPlan);
		ASSERT_TRUE(blocker);
		ASSERT_TRUE(gate.WaitForEntries(1, Milliseconds(5000)));
		const shader::ShaderBuildStatistics before = service.GetStatistics();
		const uint64 allocations = tracking.GetStatistics().LiveAllocationCount;
		bool bReachedSuccess = false;
		usize failureCount = 0;

		for (usize allowed = 0; allowed < 32; ++allowed)
		{
			memory.Reset(allowed);
			Result<shader::ShaderBuildJob> queued = service.Submit(queuedPlan);
			memory.Reset(NumericLimits<usize>::max());
			if (queued)
			{
				queued.Value().Cancel();
				queued.Value() = {};
				bReachedSuccess = true;
			}
			else
			{
				EXPECT_EQ(queued.GetError().Code, EErrorCode::ExternalFailure);
				++failureCount;
			}

			const shader::ShaderBuildStatistics after = service.GetStatistics();
			EXPECT_EQ(after.QueuedJobs, before.QueuedJobs);
			EXPECT_EQ(after.RunningJobs, before.RunningJobs);
			EXPECT_EQ(after.Subscribers, before.Subscribers);
			EXPECT_EQ(after.AdmittedSourceBytes, before.AdmittedSourceBytes);
			EXPECT_EQ(after.RetainedResultBytes, before.RetainedResultBytes);
			EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, allocations);
			if (bReachedSuccess)
			{
				break;
			}
		}

		EXPECT_TRUE(bReachedSuccess);
		EXPECT_GT(failureCount, 0u);
		Result<shader::ShaderBuildJob> retried = service.Submit(queuedPlan);
		ASSERT_TRUE(retried);
		gate.Open();
		ASSERT_TRUE(service.Drain(Milliseconds(5000)));
		EXPECT_EQ(retried.Value().Poll().State, shader::EShaderBuildJobState::Succeeded);
	}

	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, HigherPriorityQueuedJobRunsFirst)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	{
		shader::ShaderBuildPlan blockerPlan;
		shader::ShaderBuildPlan lowPlan;
		shader::ShaderBuildPlan highPlan;
		ASSERT_TRUE(prepare(memory, blockerPlan));
		ASSERT_TRUE(prepare(memory, lowPlan, "static const uint Offset = 21;\n"));
		ASSERT_TRUE(prepare(memory, highPlan, "static const uint Offset = 22;\n"));
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		shader::ShaderBuildService service(memory);
		ASSERT_TRUE(service.Start(settings(threads, gate)));
		ScopeExit cleanup(
			[&]() noexcept
			{
				gate.Open();
				EXPECT_TRUE(service.CancelAndDrain(Milliseconds(5000)));
			});
		Result<shader::ShaderBuildJob> blocker = service.Submit(blockerPlan);
		ASSERT_TRUE(blocker);
		ASSERT_TRUE(gate.WaitForEntries(1, Milliseconds(5000)));
		shader::ShaderBuildJobOptions lowOptions;
		lowOptions.Priority = shader::EShaderBuildPriority::Low;
		shader::ShaderBuildJobOptions highOptions;
		highOptions.Priority = shader::EShaderBuildPriority::High;
		Result<shader::ShaderBuildJob> low = service.Submit(lowPlan, lowOptions);
		Result<shader::ShaderBuildJob> high = service.Submit(highPlan, highOptions);
		ASSERT_TRUE(low);
		ASSERT_TRUE(high);
		gate.ReleaseOne();
		ASSERT_TRUE(gate.WaitForEntries(2, Milliseconds(5000)));
		EXPECT_EQ(high.Value().Poll().State, shader::EShaderBuildJobState::Running);
		EXPECT_EQ(low.Value().Poll().State, shader::EShaderBuildJobState::Queued);
		gate.ReleaseOne();
		ASSERT_TRUE(gate.WaitForEntries(3, Milliseconds(5000)));
		EXPECT_EQ(high.Value().Poll().State, shader::EShaderBuildJobState::Succeeded);
		EXPECT_EQ(low.Value().Poll().State, shader::EShaderBuildJobState::Running);
		gate.Open();
		EXPECT_TRUE(service.Drain(Milliseconds(5000)));
	}

	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuildService, DrainTimeoutRetainsDependenciesAndAllowsRetry)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	TrackingMemoryResource memory(SystemMemoryResource::Get());

	for (bool bCancel : {false, true})
	{
		shader::ShaderBuildPlan plan;
		ASSERT_TRUE(prepare(memory, plan));
		os::WindowsThreadBackend threads;
		test::ShaderCompilerGate gate;
		shader::ShaderBuildService service(memory);
		ASSERT_TRUE(service.Start(settings(threads, gate)));
		ScopeExit cleanup(
			[&]() noexcept
			{
				gate.Open();
				EXPECT_TRUE(service.CancelAndDrain(Milliseconds(5000)));
			});
		Result<shader::ShaderBuildJob> job = service.Submit(plan);
		ASSERT_TRUE(job);
		ASSERT_TRUE(gate.WaitForEntries(1, Milliseconds(5000)));
		Result<void> timed = bCancel ? service.CancelAndDrain(Milliseconds(0)) : service.Drain(Milliseconds(0));
		ASSERT_FALSE(timed);
		EXPECT_EQ(timed.GetError().Code, EErrorCode::Timeout);
		EXPECT_FALSE(service.GetStatistics().bDrained);
		EXPECT_TRUE(service.GetStatistics().bAdmissionClosed);
		EXPECT_FALSE(service.Submit(plan));
		gate.Open();
		ASSERT_TRUE(service.Drain(Milliseconds(5000)));
		EXPECT_EQ(job.Value().Poll().State, bCancel ? shader::EShaderBuildJobState::Cancelled : shader::EShaderBuildJobState::Succeeded);
	}

	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
