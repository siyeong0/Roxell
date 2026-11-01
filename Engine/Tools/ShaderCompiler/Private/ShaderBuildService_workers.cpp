#include <ShaderBuildService.h>

#include "ShaderBuildState.h"
#include "ShaderCompilerInternal.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ClockPrimitives.hpp>
#include <Comparison.hpp>
#include <NumericLimits.hpp>
#include <Synchronization.hpp>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		bool terminal(EShaderBuildJobState phase) noexcept
		{
			return phase == EShaderBuildJobState::Succeeded || phase == EShaderBuildJobState::Failed || phase == EShaderBuildJobState::Cancelled;
		}

		bool sameProfile(const ShaderCompilerProfile& left, const ShaderCompilerProfile& right) noexcept
		{
			return left.Profile == right.Profile && left.ProfileVersion == right.ProfileVersion && left.Language == right.Language && left.Target == right.Target &&
				left.LanguageVersion == right.LanguageVersion && left.EngineProfileVersion == right.EngineProfileVersion && left.ShaderAbiVersion == right.ShaderAbiVersion &&
				left.StageMask == right.StageMask && left.bModuleCompilation == right.bModuleCompilation && left.bLinking == right.bLinking &&
				left.bSpecialization == right.bSpecialization && left.bGenericEntrySpecialization == right.bGenericEntrySpecialization;
		}

		void copyError(BuildResultStorage& result, const Error& error, uint32 byteLimit) noexcept
		{
			result.Error.Code = error.Code;
			usize remaining = Min<usize>(byteLimit, 4096);
			auto copyText = [&](String& destination, const String& source)
			{
				usize count = Min(remaining, source.Size());

				while (count != 0 && count < source.Size() && (static_cast<unsigned char>(source.Data()[count]) & 0xc0u) == 0x80u)
				{
					--count;
				}

				result.Report.bTruncated = result.Report.bTruncated || count != source.Size();
				if (!destination.Assign({source.Data(), count}))
				{
					result.Report.bAllocationFailure = true;
				}

				remaining -= count;
			};

			copyText(result.Error.DiagnosticID, error.DiagnosticID);
			copyText(result.Error.Message, error.Message);
			copyText(result.Error.RecoveryHint, error.RecoveryHint);
		}

		void copyStartupError(Error& destination, const Error& source) noexcept
		{
			destination.Code = source.Code;
			auto copyText = [](String& target, const String& text, usize limit)
			{
				usize count = Min(text.Size(), limit);

				while (count != 0 && count < text.Size() && (static_cast<unsigned char>(text.Data()[count]) & 0xc0u) == 0x80u)
				{
					--count;
				}

				static_cast<void>(target.Assign({text.Data(), count}));
			};

			copyText(destination.DiagnosticID, source.DiagnosticID, 128);
			copyText(destination.Message, source.Message, 4096);
			copyText(destination.RecoveryHint, source.RecoveryHint, 512);
		}
	} // namespace

	void ShaderBuildService::worker() noexcept
	{
		InterfaceOwner<IShaderCompiler> compiler;
		Error startupError;
		try
		{
			Result<InterfaceOwner<IShaderCompiler>> created = m_Desc.CompilerFactory != nullptr
				? m_Desc.CompilerFactory(m_Desc.CompilerFactoryContext, m_Desc.Compiler, m_Desc.CompilerOptions, m_Resource)
				: CreateShaderCompiler(m_Desc.Compiler, m_Desc.CompilerOptions, m_Resource);
			if (created)
			{
				compiler = Move(created.Value());
			}
			else
			{
				startupError = Move(created.GetError());
			}
		}
		catch (...)
		{
			startupError = error(EErrorCode::ExternalFailure, "Shader compiler factory threw an exception.");
		}

		{
			UniqueLock lock(m_State->Mutex);
			if (!compiler && startupError.Code == EErrorCode::None)
			{
				startupError = error(EErrorCode::ExternalFailure, "Shader compiler factory returned empty ownership.");
			}

			if (compiler)
			{
				const ShaderToolchainIdentity& identity = compiler->GetToolchainIdentity();
				const Span<const ShaderCompilerProfile> profiles = compiler->GetProfiles();
				if (identity.Compiler != m_Desc.Compiler || identity.ShaderAbiVersion != m_Desc.CompilerOptions.ShaderAbiVersion || profiles.IsEmpty() || profiles.Size() > 32)
				{
					startupError = error(EErrorCode::Unsupported, "Shader worker returned invalid compiler identity or profiles.");
				}
				else if (m_State->StartedWorkers == 0)
				{
					m_State->Toolchain.Compiler = identity.Compiler;
					m_State->Toolchain.AdapterVersion = identity.AdapterVersion;
					m_State->Toolchain.ShaderAbiVersion = identity.ShaderAbiVersion;
					if (!m_State->Toolchain.CompilerVersion.Assign(identity.CompilerVersion) ||
						!m_State->Toolchain.SdkIdentity.Assign(identity.SdkIdentity) ||
						!m_State->Profiles.Reserve(compiler->GetProfiles().Size()))
					{
						startupError = MakeBuildAllocationError();
					}
					else
					{
						for (const ShaderCompilerProfile& profile : compiler->GetProfiles())
						{
							const bool bAdded = m_State->Profiles.Add(profile);
							ASSERT(bAdded, "Worker profiles are reserved before startup publication.");
						}
					}
				}
				else if (!(m_State->Toolchain == identity))
				{
					startupError = error(EErrorCode::Unsupported, "Shader service workers have different toolchain identities.");
				}
				else
				{
					bool bSameProfiles = profiles.Size() == m_State->Profiles.Size();

					for (usize i = 0; bSameProfiles && i < profiles.Size(); ++i)
					{
						bSameProfiles = sameProfile(profiles[i], m_State->Profiles[i]);
					}

					if (!bSameProfiles)
					{
						startupError = error(EErrorCode::Unsupported, "Shader workers have different supported profile rows.");
					}
				}
			}

			if (startupError.Code != EErrorCode::None && m_State->StartupError.Code == EErrorCode::None)
			{
				copyStartupError(m_State->StartupError, startupError);
			}

			++m_State->StartedWorkers;
			m_State->Changed.notify_all();
			m_State->Changed.wait(lock, [&] { return m_State->bStartReady; });
		}

		for (;;)
		{
			SharedPtr<BuildExecution> execution;
			{
				UniqueLock lock(m_State->Mutex);
				m_State->Changed.wait(lock, [&] { return m_State->bStopWorkers || m_State->Statistics.QueuedJobs != 0; });
				if (m_State->bStopWorkers && m_State->Statistics.QueuedJobs == 0)
				{
					break;
				}

				const MonotonicClock::time_point now = MonotonicClock::now();
				int64 best = NumericLimits<int64>::min();

				for (const SharedPtr<BuildExecution>& candidate : m_State->Executions)
				{
					if (candidate->bRunning || candidate->bCancellationCommitted)
					{
						continue;
					}

					int64 priority = 0;

					for (const BuildSubscriber* subscriber : candidate->Subscribers)
					{
						if (!terminal(subscriber->Phase))
						{
							priority = Max(priority, static_cast<int64>(subscriber->Options.Priority));
						}
					}

					const int64 age = DurationCast<Milliseconds>(now - candidate->QueuedAt).count();
					const int64 score = age / 250 + priority;
					if (score > best)
					{
						best = score;
						execution = candidate;
					}
				}

				ASSERT(execution, "Nonempty shader queue must have an interested execution.");
				execution->bRunning = true;
				--m_State->Statistics.QueuedJobs;
				++m_State->Statistics.RunningJobs;
				ASSERT(m_State->Statistics.CompilerExecutions != NumericLimits<uint64>::max(), "Compiler execution counter cannot overflow.");
				++m_State->Statistics.CompilerExecutions;

				for (BuildSubscriber* subscriber : execution->Subscribers)
				{
					if (subscriber->Phase == EShaderBuildJobState::Queued)
					{
						subscriber->Phase = EShaderBuildJobState::Running;
					}
				}
			}

			BuildResultStorage& result = *execution->Result;
			try
			{
				Result<ShaderBuildOutput> compiled = execution->Plan.GetKind() == EShaderBuildPlanKind::LinkedProgram
					? compiler->LinkProgram(execution->Plan, result.Report, *result.Resource, execution->Cancellation.GetToken())
					: compiler->Compile(execution->Plan, result.Report, *result.Resource, execution->Cancellation.GetToken());
				if (compiled)
				{
					ASSERT(
						compiled.Value().Artifact.IsValid() &&
						compiled.Value().BuildIdentity == execution->Plan.GetBuildIdentity(),
						"Successful prepared compilation must publish a valid artifact with its plan "
						"identity.");
					result.Output = Move(compiled.Value());
					result.bHasOutput = true;
				}
				else
				{
					copyError(result, compiled.GetError(), execution->Plan.GetRequest().Limits.MaximumDiagnosticBytes);
				}
			}
			catch (...)
			{
				copyError(result, error(EErrorCode::ExternalFailure, "Shader compiler execution threw an exception."), execution->Plan.GetRequest().Limits.MaximumDiagnosticBytes);
			}

			result.Resource->Publish();
			result.RetainedBytes = result.Resource->GetUsedBytes() + RESULT_OWNER_BYTE_CHARGE;
			{
				LockGuard lock(m_State->Mutex);
				const MonotonicClock::time_point now = MonotonicClock::now();

				for (BuildSubscriber* subscriber : execution->Subscribers)
				{
					if (!terminal(subscriber->Phase) && subscriber->Options.bHasDeadline && now >= subscriber->Options.Deadline)
					{
						static_cast<void>(m_State->CancelLocked(*subscriber));
					}
				}

				m_State->PublishLocked(*execution);
				--m_State->Statistics.RunningJobs;
				m_State->Statistics.AdmittedSourceBytes -= execution->SourceBytes;
				m_State->Executions.RemoveIf([&](const SharedPtr<BuildExecution>& candidate) noexcept { return candidate.Get() == execution.Get(); });
				execution->Plan = {};
				execution->Result.Reset();
			}

			m_State->Changed.notify_all();
		}

		compiler.Reset();
		{
			LockGuard lock(m_State->Mutex);
			++m_State->ExitedThreads;
		}

		m_State->Changed.notify_all();
	}

	void ShaderBuildService::deadlines() noexcept
	{
		UniqueLock lock(m_State->Mutex);
		m_State->Changed.wait(lock, [&] { return m_State->bStartReady; });

		for (;;)
		{
			if (m_State->bStopWorkers && m_State->Executions.IsEmpty())
			{
				break;
			}

			MonotonicClock::time_point nearest = MonotonicClock::time_point::max();
			SharedPtr<BuildExecution> cancel;
			const MonotonicClock::time_point now = MonotonicClock::now();

			for (const SharedPtr<BuildExecution>& execution : m_State->Executions)
			{
				if (execution->bCancellationCommitted && !execution->bCancellationDispatched)
				{
					cancel = execution;
					break;
				}

				for (BuildSubscriber* subscriber : execution->Subscribers)
				{
					if (terminal(subscriber->Phase) || !subscriber->Options.bHasDeadline)
					{
						continue;
					}

					if (subscriber->Options.Deadline <= now)
					{
						cancel = execution;
						break;
					}

					nearest = Min(nearest, subscriber->Options.Deadline);
				}

				if (cancel)
				{
					break;
				}
			}

			if (cancel)
			{
				for (BuildSubscriber* subscriber : cancel->Subscribers)
				{
					if (!terminal(subscriber->Phase) && subscriber->Options.bHasDeadline && subscriber->Options.Deadline <= now)
					{
						static_cast<void>(m_State->CancelLocked(*subscriber));
					}
				}

				const bool bRequest = cancel->bCancellationCommitted && !cancel->bCancellationDispatched;
				cancel->bCancellationDispatched = cancel->bCancellationCommitted;
				lock.unlock();
				m_State->Changed.notify_all();
				if (bRequest)
				{
					cancel->Cancellation.RequestCancellation();
				}

				lock.lock();
				continue;
			}

			if (nearest == MonotonicClock::time_point::max())
			{
				m_State->Changed.wait(lock);
			}
			else
			{
				m_State->Changed.wait_until(lock, nearest);
			}
		}

		++m_State->ExitedThreads;
		lock.unlock();
		m_State->Changed.notify_all();
	}

} // namespace rox::shader
