#include "AftermathBackend.h"

#include <ClockPrimitives.hpp>
#include <ThreadPrimitives.hpp>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>
#include <SystemError.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <Utility.hpp>
#include <Array.hpp>
#include <Assertion.h>
#include <CoreString.hpp>

#ifndef ROXELL_RHI_WITH_AFTERMATH
#define ROXELL_RHI_WITH_AFTERMATH 0
#endif

#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <GFSDK_Aftermath.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>
#endif

namespace rox::rhi
{
	using namespace core;

	namespace
	{
		Atomic<bool> g_DiagnosticsOwned{ false };

		[[nodiscard]] Error error(EErrorCode code, const char* diagnostic, const char* message) noexcept
		{
			return MakeError(code, StringView(diagnostic, CStringLength(diagnostic)),
				StringView(message, CStringLength(message)));
		}

		[[nodiscard]] Error sdkError(EErrorCode code, const char* diagnostic, const char* message, uint32 sdkResult) noexcept
		{
			char text[192]{};
			const usize prefix = CStringLength(message);
			ASSERT(prefix + 24 < sizeof(text), "An SDK diagnostic must fit its buffer.");
			CopyBytes(text, message, prefix);
			CopyBytes(text + prefix, " Result=0x", 10);
			const ToCharsResult rendered = ToChars(text + prefix + 10, text + sizeof(text), sdkResult, 16);
			ASSERT(rendered.ec == EErrorCondition(), "An SDK result must render into its buffer.");

			return MakeError(code, StringView(diagnostic, CStringLength(diagnostic)),
				StringView(text, static_cast<usize>(rendered.ptr - text)));
		}

		struct MarkerEntry final
		{
			explicit MarkerEntry(MemoryResource& resource) noexcept
				: Name(resource)
			{
			}

			uint64 Token = 0;
			String Name;
		};

		struct ShaderEntry final
		{
			explicit ShaderEntry(MemoryResource& resource) noexcept
				: Bytecode(resource)
			{
			}

			uint64 Token = 0;
			uint64 Hash = 0;
			EBackend Backend = EBackend::Null;
			Array<Byte> Bytecode;
		};

		class AftermathDiagnostics final : public IGpuCrashDiagnostics
		{
		public:
			AftermathDiagnostics(MemoryResource& resource, GpuCrashDiagnosticsDesc desc) noexcept
				: m_Resource(resource)
				, m_Desc(desc)
				, m_Markers(resource)
				, m_Shaders(resource)
				, m_LastDump(resource)
			{
			}

			~AftermathDiagnostics() override
			{
				ASSERT(m_ActiveDevices.load(MemoryOrderAcquire) == 0, "GPU diagnostics owner must outlive every attached device.");
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				if (m_Enabled && m_DeviceLost.load(MemoryOrderAcquire))
				{
					WaitForCrashDump();
				}

				if (m_Enabled && m_Disable != nullptr)
				{
					(void)m_Disable();
				}
				{
					UniqueLock lock(m_Mutex);
					m_CallbacksFinished.wait(lock, [&] { return m_ActiveCallbacks.load(MemoryOrderAcquire) == 0; });
				}

				if (m_Library != nullptr)
				{
					FreeLibrary(m_Library);
				}
#endif
				g_DiagnosticsOwned.store(false, MemoryOrderRelease);
			}

			[[nodiscard]] Result<void> Initialize(const char* libraryPath)
			{
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				if (libraryPath == nullptr || libraryPath[0] == '\0')
				{
					m_Library = LoadLibraryExW(L"GFSDK_Aftermath_Lib.x64.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
				}
				else
				{
					const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, libraryPath, -1, nullptr, 0);

					if (length <= 0)
					{
						return Result<void>::Failure(error(EErrorCode::InvalidArgument, "Rhi.AftermathPath", "The SDK library path is not valid UTF-8."));
					}

					Array<wchar_t> wide(m_Resource);

					if (!wide.ResizeUninitialized(static_cast<usize>(length)))
					{
						return Result<void>::Failure(error(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not store the SDK library path."));
					}

					if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, libraryPath, -1, wide.Data(), length) != length)
					{
						return Result<void>::Failure(error(EErrorCode::InvalidArgument, "Rhi.AftermathPath", "The SDK library path is not valid UTF-8."));
					}

					m_Library = LoadLibraryExW(wide.Data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
				}

				if (m_Library == nullptr)
				{
					return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathLibrary", "The Nsight Aftermath runtime is unavailable."));
				}

				m_Enable = Load<PFN_GFSDK_Aftermath_EnableGpuCrashDumps>("GFSDK_Aftermath_EnableGpuCrashDumps");
				m_Disable = Load<PFN_GFSDK_Aftermath_DisableGpuCrashDumps>("GFSDK_Aftermath_DisableGpuCrashDumps");
				m_Status = Load<PFN_GFSDK_Aftermath_GetCrashDumpStatus>("GFSDK_Aftermath_GetCrashDumpStatus");
				m_D3D12Initialize = Load<PFN_GFSDK_Aftermath_DX12_Initialize>("GFSDK_Aftermath_DX12_Initialize");
				m_CreateContext = Load<PFN_GFSDK_Aftermath_DX12_CreateContextHandle>("GFSDK_Aftermath_DX12_CreateContextHandle");
				m_ReleaseContext = Load<PFN_GFSDK_Aftermath_ReleaseContextHandle>("GFSDK_Aftermath_ReleaseContextHandle");
				m_SetMarker = Load<PFN_GFSDK_Aftermath_SetEventMarker>("GFSDK_Aftermath_SetEventMarker");
				m_UpdateResource = Load<PFN_GFSDK_Aftermath_DX12_UpdateResourceInfo>("GFSDK_Aftermath_DX12_UpdateResourceInfo");
				m_D3D12ShaderHash = Load<PFN_GFSDK_Aftermath_GetShaderHash>("GFSDK_Aftermath_GetShaderHash");
				m_VulkanShaderHash = Load<PFN_GFSDK_Aftermath_GetShaderHashSpirv>("GFSDK_Aftermath_GetShaderHashSpirv");

				if (m_Enable == nullptr ||
					m_Disable == nullptr ||
					m_Status == nullptr ||
					m_D3D12Initialize == nullptr ||
					m_CreateContext == nullptr ||
					m_ReleaseContext == nullptr ||
					m_SetMarker == nullptr ||
					m_UpdateResource == nullptr ||
					m_D3D12ShaderHash == nullptr ||
					m_VulkanShaderHash == nullptr)
				{
					return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathSymbols", "The Nsight Aftermath runtime lacks required exports."));
				}

				const uint32 flags = m_Desc.bShaderDebugInfo ? GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks : GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default;
				const GFSDK_Aftermath_Result result =
					m_Enable(GFSDK_Aftermath_Version_API,
						GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX | GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan, flags,
						CrashDumpCallback, m_Desc.bShaderDebugInfo ? ShaderDebugInfoCallback : nullptr, nullptr,
						m_Desc.bMarkers ? ResolveMarkerCallback : nullptr, this);

				if (!GFSDK_Aftermath_SUCCEED(result))
				{
					return Result<void>::Failure(
						sdkError(EErrorCode::Unsupported, "Rhi.AftermathEnable", "The driver rejected Nsight Aftermath initialization.", static_cast<uint32>(result)));
				}

				m_Enabled = true;

				return Result<void>::Success();
#else
				(void)libraryPath;

				return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathBuild", "This RHI build has no Nsight Aftermath SDK support."));
#endif
			}

			[[nodiscard]] bool IsAftermathEnabled() const noexcept override { return m_Enabled; }

			[[nodiscard]] Result<EGpuCrashDumpStatus> GetCrashDumpStatus() const override
			{
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				if (!m_Enabled)
				{
					return Result<EGpuCrashDumpStatus>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathStatus", "Nsight Aftermath is disabled."));
				}
				GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;

				if (!GFSDK_Aftermath_SUCCEED(m_Status(&status)))
				{
					return Result<EGpuCrashDumpStatus>::Failure(error(EErrorCode::ExternalFailure, "Rhi.AftermathStatus", "Could not query GPU crash dump status."));
				}

				switch (status)
				{
				case GFSDK_Aftermath_CrashDump_Status_NotStarted:
					return Result<EGpuCrashDumpStatus>::Success(EGpuCrashDumpStatus::NotStarted);
				case GFSDK_Aftermath_CrashDump_Status_CollectingData:
				case GFSDK_Aftermath_CrashDump_Status_InvokingCallback:
					return Result<EGpuCrashDumpStatus>::Success(EGpuCrashDumpStatus::Collecting);
				case GFSDK_Aftermath_CrashDump_Status_Finished:
					return Result<EGpuCrashDumpStatus>::Success(EGpuCrashDumpStatus::Finished);
				case GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed:
					return Result<EGpuCrashDumpStatus>::Success(EGpuCrashDumpStatus::Failed);
				default:
					return Result<EGpuCrashDumpStatus>::Success(EGpuCrashDumpStatus::Unknown);
				}
#else

				return Result<EGpuCrashDumpStatus>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathBuild", "This RHI build has no Nsight Aftermath SDK support."));
#endif
			}

			[[nodiscard]] Result<uint64> CopyLatestCrashDump(Span<Byte> output) const override
			{
				LockGuard lock(m_Mutex);

				if (!output.IsEmpty() && output.Size() < m_LastDump.Size())
				{
					return Result<uint64>::Failure(error(EErrorCode::InvalidArgument, "Rhi.AftermathDump", "The crash dump output is too small."));
				}

				if (!output.IsEmpty() && !m_LastDump.IsEmpty())
				{
					CopyBytes(output.Data(), m_LastDump.Data(), m_LastDump.Size());
				}

				return Result<uint64>::Success(m_LastDump.Size());
			}

			[[nodiscard]] Result<uint64> RegisterMarker(StringView text) override
			{
				if (!m_Desc.bMarkers || text.IsEmpty() || text.Size() > 1024)
				{
					return Result<uint64>::Failure(error(EErrorCode::InvalidArgument, "Rhi.AftermathMarker", "The marker text is invalid or markers are disabled."));
				}

				LockGuard lock(m_Mutex);

				if (m_Markers.Size() >= m_Desc.MaximumMarkers || m_NextToken == UINT64_MAX)
				{
					return Result<uint64>::Failure(error(EErrorCode::Conflict, "Rhi.AftermathMarker", "The marker registry is full."));
				}
				MarkerEntry marker(m_Resource);

				if (!marker.Name.Assign(text))
				{
					return Result<uint64>::Failure(error(EErrorCode::ExternalFailure, "Rhi.AftermathMarker", "Could not copy marker text."));
				}
				marker.Token = m_NextToken;

				if (!m_Markers.Add(Move(marker)))
				{
					return Result<uint64>::Failure(error(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain the marker."));
				}

				return Result<uint64>::Success(m_NextToken++);
			}

			[[nodiscard]] Result<void> RetireMarker(uint64 token) override
			{
				LockGuard lock(m_Mutex);

				for (usize index = 0; index < m_Markers.Size(); ++index)
				{
					if (m_Markers[index].Token != token)
					{
						continue;
					}

					if (!m_DeviceLost.load(MemoryOrderAcquire))
					{
						m_Markers.RemoveAtSwap(index);
					}

					return Result<void>::Success();
				}

				return Result<void>::Failure(error(EErrorCode::NotFound, "Rhi.AftermathMarker", "The marker token is unknown."));
			}

			[[nodiscard]] Result<uint64> RegisterShaderBinary(EBackend backend, uint64 shaderHash, Span<const Byte> bytecode) override
			{
				if ((backend != EBackend::D3D12 && backend != EBackend::Vulkan) || shaderHash == 0 || bytecode.IsEmpty())
				{
					return Result<uint64>::Failure(error(EErrorCode::InvalidArgument, "Rhi.AftermathShader", "The shader identity or bytecode is invalid."));
				}

				LockGuard lock(m_Mutex);

				if (m_ShaderBytes > m_Desc.MaximumShaderBinaryBytes || bytecode.Size() > m_Desc.MaximumShaderBinaryBytes - m_ShaderBytes || m_NextToken == UINT64_MAX)
				{
					return Result<uint64>::Failure(error(EErrorCode::Conflict, "Rhi.AftermathShader", "The shader registry budget is full."));
				}
				ShaderEntry shader(m_Resource);

				if (!shader.Bytecode.ResizeUninitialized(bytecode.Size()))
				{
					return Result<uint64>::Failure(error(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not copy shader bytecode."));
				}

				CopyBytes(shader.Bytecode.Data(), bytecode.Data(), bytecode.Size());
				shader.Token = m_NextToken;
				shader.Hash = shaderHash;
				shader.Backend = backend;

				if (!m_Shaders.Add(Move(shader)))
				{
					return Result<uint64>::Failure(error(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain shader bytecode."));
				}

				m_ShaderBytes += bytecode.Size();

				return Result<uint64>::Success(m_NextToken++);
			}

			[[nodiscard]] Result<uint64> ComputeShaderBinaryHash(EBackend backend, Span<const Byte> bytecode) const override
			{
				if ((backend != EBackend::D3D12 && backend != EBackend::Vulkan) || bytecode.IsEmpty() || bytecode.Size() > UINT32_MAX)
				{
					return Result<uint64>::Failure(error(EErrorCode::InvalidArgument, "Rhi.AftermathShader", "The shader backend or bytecode is invalid."));
				}
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				GFSDK_Aftermath_ShaderBinaryHash hash{};
				GFSDK_Aftermath_Result result{};
				if (backend == EBackend::D3D12)
				{
					const D3D12_SHADER_BYTECODE shader{ bytecode.Data(), bytecode.Size() };
					result = m_D3D12ShaderHash(GFSDK_Aftermath_Version_API, &shader, &hash);
				}
				else
				{
					const GFSDK_Aftermath_SpirvCode shader{ bytecode.Data(), static_cast<uint32>(bytecode.Size()) };
					result = m_VulkanShaderHash(GFSDK_Aftermath_Version_API, &shader, &hash);
				}

				if (!GFSDK_Aftermath_SUCCEED(result))
				{
					return Result<uint64>::Failure(
						sdkError(EErrorCode::Unsupported, "Rhi.AftermathShader", "The SDK could not hash shader bytecode.", static_cast<uint32>(result)));
				}

				return Result<uint64>::Success(hash.hash);
#else

				return Result<uint64>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathBuild", "This RHI build has no Nsight Aftermath SDK support."));
#endif
			}

			[[nodiscard]] Result<void> UnregisterShaderBinary(uint64 token) override
			{
				LockGuard lock(m_Mutex);

				for (usize index = 0; index < m_Shaders.Size(); ++index)
				{
					if (m_Shaders[index].Token != token)
					{
						continue;
					}

					if (!m_DeviceLost.load(MemoryOrderAcquire))
					{
						m_ShaderBytes -= m_Shaders[index].Bytecode.Size();
						m_Shaders.RemoveAtSwap(index);
					}

					return Result<void>::Success();
				}

				return Result<void>::Failure(error(EErrorCode::NotFound, "Rhi.AftermathShader", "The shader token is unknown."));
			}

			[[nodiscard]] Result<uint64> CopyShaderBinary(EBackend backend, uint64 shaderHash, Span<Byte> output) const override
			{
				LockGuard lock(m_Mutex);

				for (const ShaderEntry& shader : m_Shaders)
				{
					if (shader.Backend != backend || shader.Hash != shaderHash)
					{
						continue;
					}

					if (!output.IsEmpty() && output.Size() < shader.Bytecode.Size())
					{
						return Result<uint64>::Failure(error(EErrorCode::InvalidArgument, "Rhi.AftermathShader", "The shader output is too small."));
					}

					if (!output.IsEmpty())
					{
						CopyBytes(output.Data(), shader.Bytecode.Data(), shader.Bytecode.Size());
					}

					return Result<uint64>::Success(shader.Bytecode.Size());
				}

				return Result<uint64>::Failure(error(EErrorCode::NotFound, "Rhi.AftermathShader", "The shader hash is unknown."));
			}

			[[nodiscard]] AftermathVulkanOptions VulkanOptions() const noexcept
			{
				if (!m_Enabled)
				{
					return {};
				}

				return { m_Desc.bMarkers, m_Desc.bResourceTracking, m_Desc.bShaderDebugInfo, m_Desc.bShaderErrorReporting };
			}

			[[nodiscard]] Result<void> AttachD3D12(void* nativeDevice)
			{
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				if (!m_Enabled || nativeDevice == nullptr || m_D3D12Attached.exchange(true))
				{
					return Result<void>::Failure(error(EErrorCode::Conflict, "Rhi.AftermathD3D12", "Only the first initialized D3D12 device is supported."));
				}

				uint32 flags = 0;

				if (m_Desc.bMarkers)
				{
					flags |= GFSDK_Aftermath_FeatureFlags_EnableMarkers;
				}

				if (m_Desc.bResourceTracking)
				{
					flags |= GFSDK_Aftermath_FeatureFlags_EnableResourceTracking;
				}

				if (m_Desc.bShaderDebugInfo)
				{
					flags |= GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo;
				}

				if (m_Desc.bShaderErrorReporting)
				{
					flags |= GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;
				}

				const GFSDK_Aftermath_Result result = m_D3D12Initialize(GFSDK_Aftermath_Version_API, flags, static_cast<ID3D12Device*>(nativeDevice));

				if (!GFSDK_Aftermath_SUCCEED(result))
				{
					m_D3D12Attached.store(false, MemoryOrderRelease);

					return Result<void>::Failure(
						sdkError(EErrorCode::Unsupported, "Rhi.AftermathD3D12", "The D3D12 device could not enable Nsight Aftermath.", static_cast<uint32>(result)));
				}

				m_ActiveDevices.fetch_add(1, MemoryOrderAcqRel);

				return Result<void>::Success();
#else
				(void)nativeDevice;

				return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathBuild", "This RHI build has no Nsight Aftermath SDK support."));
#endif
			}

			[[nodiscard]] Result<void> AttachVulkan(bool bDiagnosticsConfig, bool bDiagnosticCheckpoints)
			{
				if (!m_Enabled || ((m_Desc.bResourceTracking || m_Desc.bShaderDebugInfo || m_Desc.bShaderErrorReporting) && !bDiagnosticsConfig) || (m_Desc.bMarkers && !bDiagnosticCheckpoints))
				{
					return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathVulkan", "The Vulkan device lacks requested diagnostics extensions."));
				}

				m_ActiveDevices.fetch_add(1, MemoryOrderAcqRel);

				return Result<void>::Success();
			}

			void DetachDevice() noexcept
			{
				uint32 const previous = m_ActiveDevices.fetch_sub(1, MemoryOrderAcqRel);
				ASSERT(previous != 0, "An Aftermath device must attach before detaching.");
			}

			void NotifyDeviceLost() noexcept
			{
				m_DeviceLost.store(true, MemoryOrderRelease);
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				WaitForCrashDump();
#endif
			}

			[[nodiscard]] Result<void*> CreateD3D12Context(void* commandList)
			{
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				if (!m_Enabled || !m_Desc.bMarkers || commandList == nullptr)
				{
					return Result<void*>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathContext", "Aftermath command markers are unavailable."));
				}
				GFSDK_Aftermath_ContextHandle context{};
				if (!GFSDK_Aftermath_SUCCEED(m_CreateContext(static_cast<IUnknown*>(commandList), &context)))
				{
					return Result<void*>::Failure(error(EErrorCode::ExternalFailure, "Rhi.AftermathContext", "Could not create an Aftermath command context."));
				}

				return Result<void*>::Success(context);
#else
				(void)commandList;

				return Result<void*>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathBuild", "This RHI build has no Nsight Aftermath SDK support."));
#endif
			}

			void ReleaseD3D12Context(void* context) noexcept
			{
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				if (context != nullptr)
				{
					(void)m_ReleaseContext(static_cast<GFSDK_Aftermath_ContextHandle>(context));
				}
#else
				(void)context;
#endif
			}

			[[nodiscard]] Result<void> SetD3D12Marker(void* context, uint64 token)
			{
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				if (!m_Enabled || !m_Desc.bMarkers || context == nullptr || token == 0 || token > UINTPTR_MAX)
				{
					return Result<void>::Failure(error(EErrorCode::InvalidArgument, "Rhi.AftermathMarker", "The Aftermath context or token is invalid."));
				}

				const GFSDK_Aftermath_Result result = m_SetMarker(static_cast<GFSDK_Aftermath_ContextHandle>(context), reinterpret_cast<const void*>(static_cast<uintptr>(token)), 0);

				if (!GFSDK_Aftermath_SUCCEED(result))
				{
					return Result<void>::Failure(error(EErrorCode::ExternalFailure, "Rhi.AftermathMarker", "Could not record an Aftermath marker."));
				}

				return Result<void>::Success();
#else
				(void)context;
				(void)token;

				return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathBuild", "This RHI build has no Nsight Aftermath SDK support."));
#endif
			}

			[[nodiscard]] Result<void> UpdateD3D12Resource(void* nativeResource)
			{
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
				if (!m_Enabled || !m_Desc.bResourceTracking || nativeResource == nullptr)
				{
					return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathResource", "Aftermath resource tracking is unavailable."));
				}

				if (!GFSDK_Aftermath_SUCCEED(m_UpdateResource(static_cast<ID3D12Resource*>(nativeResource))))
				{
					return Result<void>::Failure(error(EErrorCode::ExternalFailure, "Rhi.AftermathResource", "Could not update resource metadata."));
				}

				return Result<void>::Success();
#else
				(void)nativeResource;

				return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathBuild", "This RHI build has no Nsight Aftermath SDK support."));
#endif
			}

		private:
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)
			template <typename T> [[nodiscard]] T Load(const char* name) const noexcept
			{
				return reinterpret_cast<T>(GetProcAddress(m_Library, name));
			}

			void WaitForCrashDump() const noexcept
			{
				const MonotonicClock::time_point deadline = MonotonicClock::now() + Seconds(2);

				while (MonotonicClock::now() < deadline)
				{
					GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;

					if (!GFSDK_Aftermath_SUCCEED(m_Status(&status)) || status == GFSDK_Aftermath_CrashDump_Status_Finished || status == GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed)
					{
						break;
					}

					SleepFor(Milliseconds(50));
				}
			}

			void CallbackStarted() noexcept
			{
				LockGuard lock(m_Mutex);
				m_ActiveCallbacks.fetch_add(1, MemoryOrderAcqRel);
			}

			static void GFSDK_AFTERMATH_CALL CrashDumpCallback(const void* data, uint32 size, void* user)
			{
				AftermathDiagnostics& self = *static_cast<AftermathDiagnostics*>(user);
				self.CallbackStarted();

				if (data != nullptr && size != 0 && size <= self.m_Desc.MaximumCrashDumpBytes)
				{
					Array<Byte> copy(self.m_Resource);

					if (copy.ResizeUninitialized(size))
					{
						CopyBytes(copy.Data(), data, size);

						if (self.m_Desc.OnCrashDump != nullptr)
						{
							self.m_Desc.OnCrashDump(self.m_Desc.CallbackUser, copy.View());
						}

						LockGuard lock(self.m_Mutex);
						self.m_LastDump = Move(copy);
					}
				}
				self.CallbackFinished();
			}

			static void GFSDK_AFTERMATH_CALL ShaderDebugInfoCallback(const void* data, uint32 size, void* user)
			{
				AftermathDiagnostics& self = *static_cast<AftermathDiagnostics*>(user);
				self.CallbackStarted();

				if (data != nullptr && size != 0 && self.m_Desc.OnShaderDebugInfo != nullptr && size <= self.m_Desc.MaximumCrashDumpBytes)
				{
					Array<Byte> copy(self.m_Resource);

					if (copy.ResizeUninitialized(size))
					{
						CopyBytes(copy.Data(), data, size);
						self.m_Desc.OnShaderDebugInfo(self.m_Desc.CallbackUser, copy.View());
					}
				}
				self.CallbackFinished();
			}

			static void GFSDK_AFTERMATH_CALL ResolveMarkerCallback(const void* token, uint32 dataSize, void* user, PFN_GFSDK_Aftermath_ResolveMarker resolve)
			{
				AftermathDiagnostics& self = *static_cast<AftermathDiagnostics*>(user);
				self.CallbackStarted();

				if (dataSize == 0 && resolve != nullptr)
				{
					const uint64 value = static_cast<uint64>(reinterpret_cast<uintptr>(token));
					LockGuard lock(self.m_Mutex);

					for (const MarkerEntry& marker : self.m_Markers)
					{
						if (marker.Token != value)
						{
							continue;
						}

						resolve(marker.Name.Data(), static_cast<uint32>(marker.Name.Size()));
						break;
					}
				}
				self.CallbackFinished();
			}

			void CallbackFinished() noexcept
			{
				LockGuard lock(m_Mutex);
				m_ActiveCallbacks.fetch_sub(1, MemoryOrderAcqRel);
				m_CallbacksFinished.notify_all();
			}
#endif

			MemoryResource& m_Resource;
			GpuCrashDiagnosticsDesc m_Desc;

			mutable Mutex m_Mutex;
			ConditionVariable m_CallbacksFinished;

			Array<MarkerEntry> m_Markers;
			Array<ShaderEntry> m_Shaders;

			Array<Byte> m_LastDump;

			uint64 m_NextToken = 1;
			uint64 m_ShaderBytes = 0;

			Atomic<uint32> m_ActiveDevices{ 0 };
			Atomic<uint32> m_ActiveCallbacks{ 0 };

			Atomic<bool> m_DeviceLost{ false };
			Atomic<bool> m_D3D12Attached{ false };
			bool m_Enabled = false;
#if ROXELL_RHI_WITH_AFTERMATH && defined(_WIN32)

			HMODULE m_Library = nullptr;

			PFN_GFSDK_Aftermath_EnableGpuCrashDumps m_Enable = nullptr;
			PFN_GFSDK_Aftermath_DisableGpuCrashDumps m_Disable = nullptr;
			PFN_GFSDK_Aftermath_GetCrashDumpStatus m_Status = nullptr;

			PFN_GFSDK_Aftermath_DX12_Initialize m_D3D12Initialize = nullptr;
			PFN_GFSDK_Aftermath_DX12_CreateContextHandle m_CreateContext = nullptr;
			PFN_GFSDK_Aftermath_ReleaseContextHandle m_ReleaseContext = nullptr;
			PFN_GFSDK_Aftermath_SetEventMarker m_SetMarker = nullptr;
			PFN_GFSDK_Aftermath_DX12_UpdateResourceInfo m_UpdateResource = nullptr;

			PFN_GFSDK_Aftermath_GetShaderHash m_D3D12ShaderHash = nullptr;
			PFN_GFSDK_Aftermath_GetShaderHashSpirv m_VulkanShaderHash = nullptr;
#endif
		};

		[[nodiscard]] AftermathDiagnostics* impl(IGpuCrashDiagnostics* diagnostics) noexcept
		{
			AftermathDiagnostics* const implementation = dynamic_cast<AftermathDiagnostics*>(diagnostics);
			ASSERT(implementation != nullptr, "RHI diagnostics must originate from CreateGpuCrashDiagnostics.");

			return implementation;
		}
	} // namespace

	Result<InterfaceOwner<IGpuCrashDiagnostics>> CreateAftermathDiagnostics(GpuCrashDiagnosticsDesc desc, MemoryResource& resource)
	{
		bool bExpected = false;

		if (!g_DiagnosticsOwned.compare_exchange_strong(bExpected, true, MemoryOrderAcqRel))
		{
			return Result<InterfaceOwner<IGpuCrashDiagnostics>>::Failure(error(EErrorCode::Conflict, "Rhi.AftermathOwner", "Only one process-wide GPU diagnostics owner may exist."));
		}

		InterfaceOwner<IGpuCrashDiagnostics> owner = InterfaceOwner<IGpuCrashDiagnostics>::Create<AftermathDiagnostics>(resource, resource, desc);

		if (!owner)
		{
			g_DiagnosticsOwned.store(false, MemoryOrderRelease);

			return Result<InterfaceOwner<IGpuCrashDiagnostics>>::Failure(error(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate GPU diagnostics."));
		}

		const Result<void> initialized = impl(owner.Get())->Initialize(desc.RuntimeLibraryPath);

		if (!initialized)
		{
			return Result<InterfaceOwner<IGpuCrashDiagnostics>>::Failure(initialized.GetError());
		}

		return Result<InterfaceOwner<IGpuCrashDiagnostics>>::Success(Move(owner));
	}

	AftermathVulkanOptions GetAftermathVulkanOptions(IGpuCrashDiagnostics* diagnostics) noexcept
	{
		return diagnostics != nullptr ? impl(diagnostics)->VulkanOptions() : AftermathVulkanOptions{};
	}

	Result<void> AttachAftermathD3D12(IGpuCrashDiagnostics* diagnostics, void* nativeDevice)
	{
		if (diagnostics == nullptr)
		{
			return Result<void>::Success();
		}

		return impl(diagnostics)->AttachD3D12(nativeDevice);
	}

	Result<void> AttachAftermathVulkan(IGpuCrashDiagnostics* diagnostics, bool bDiagnosticsConfig, bool bDiagnosticCheckpoints)
	{
		if (diagnostics == nullptr)
		{
			return Result<void>::Success();
		}

		return impl(diagnostics)->AttachVulkan(bDiagnosticsConfig, bDiagnosticCheckpoints);
	}

	void DetachAftermathDevice(IGpuCrashDiagnostics* diagnostics) noexcept
	{
		if (diagnostics != nullptr)
		{
			impl(diagnostics)->DetachDevice();
		}
	}

	void NotifyAftermathDeviceLost(IGpuCrashDiagnostics* diagnostics) noexcept
	{
		if (diagnostics != nullptr)
		{
			impl(diagnostics)->NotifyDeviceLost();
		}
	}

	Result<void*> CreateAftermathD3D12Context(IGpuCrashDiagnostics* diagnostics, void* recordingCommandList)
	{
		if (diagnostics == nullptr)
		{
			return Result<void*>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathContext", "GPU diagnostics were not requested."));
		}

		return impl(diagnostics)->CreateD3D12Context(recordingCommandList);
	}

	void ReleaseAftermathD3D12Context(IGpuCrashDiagnostics* diagnostics, void* nativeContext) noexcept
	{
		if (diagnostics != nullptr)
		{
			impl(diagnostics)->ReleaseD3D12Context(nativeContext);
		}
	}

	Result<void> SetAftermathD3D12Marker(IGpuCrashDiagnostics* diagnostics, void* nativeContext, uint64 markerToken)
	{
		if (diagnostics == nullptr)
		{
			return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathMarker", "GPU diagnostics were not requested."));
		}

		return impl(diagnostics)->SetD3D12Marker(nativeContext, markerToken);
	}

	Result<void> UpdateAftermathD3D12Resource(IGpuCrashDiagnostics* diagnostics, void* nativeResource)
	{
		if (diagnostics == nullptr)
		{
			return Result<void>::Failure(error(EErrorCode::Unsupported, "Rhi.AftermathResource", "GPU diagnostics were not requested."));
		}

		return impl(diagnostics)->UpdateD3D12Resource(nativeResource);
	}
}
