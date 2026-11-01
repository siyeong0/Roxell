#include <WindowsWindowSystem.h>
#include <Synchronization.hpp>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <ScopeExit.hpp>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <thread>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;
	using Microsoft::WRL::ComPtr;

	TEST(WindowsAccessibilityTests, InvokeProducesARequestThatCanBeginOnceAndComplete)
	{
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		{
			os::WindowsWindowSystem system;
			ASSERT_TRUE(os::WindowsWindowSystem::TryCreate(memory, {}, &system));
			os::WindowDesc desc;
			desc.Title = "Roxell accessibility request test";
			desc.ClientExtent = { 320, 200 };
			desc.bVisible = true;
			core::Result<os::WindowId> created = system.CreateWindow(desc);
			ASSERT_TRUE(created);
			const os::WindowId window = created.Value();
			bool bWindowAlive = true;
			core::ScopeExit destroy([&]() noexcept
			{
				if (bWindowAlive)
				{
					(void)system.DestroyWindow(window);
				}
			});
			const core::Result<os::NativeWindowHandle> native = system.GetNativeHandle(window);
			ASSERT_TRUE(native);
			const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
			core::Array<os::AccessibilityNode> nodes(memory);
			os::AccessibilityNode root(memory);
			root.Id = { 1, 1, 1, 0 };
			nodes.AddRequired(core::Move(root));
			os::AccessibilityNode button(memory);
			button.Id = { 1, 1, 1, 1 };
			button.Parent = { 1, 1, 1, 0 };
			button.Role = os::EAccessibilityRole::Button;
			ASSERT_TRUE(button.Name.Assign("Run"));
			nodes.AddRequired(core::Move(button));
			core::Result<os::AccessibilitySnapshot> snapshot = os::AccessibilitySnapshot::Create(42, core::Move(nodes));
			ASSERT_TRUE(snapshot);
			ASSERT_TRUE(system.SetAccessibilitySnapshot(window, snapshot.Value()));
			rox::core::Atomic<bool> bDone{ false };
			rox::core::Atomic<int> clientResult{ 0 };
			rox::core::Atomic<HRESULT> clientStatus{ S_OK };
			std::thread client([&]()
			{
				core::ScopeExit done([&]() noexcept { bDone.store(true); });
				if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
				{
					clientResult.store(1);
					return;
				}
				core::ScopeExit apartment([]() noexcept { CoUninitialize(); });
				ComPtr<IUIAutomation> automation;
				if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))))
				{
					clientResult.store(2);
					return;
				}
				ComPtr<IUIAutomationElement> rootElement;
				HRESULT rootStatus = E_FAIL;
				const rox::core::MonotonicClock::time_point rootDeadline = rox::core::MonotonicClock::now() + rox::core::Seconds(3);
				do
				{
					rootElement.Reset();
					rootStatus = automation->ElementFromHandle(handle, &rootElement);
					if (SUCCEEDED(rootStatus) && rootElement)
					{
						break;
					}
					rox::core::SleepFor(rox::core::Milliseconds(10));
				} while (rox::core::MonotonicClock::now() < rootDeadline);
				if (FAILED(rootStatus) || !rootElement)
				{
					clientStatus.store(rootStatus);
					clientResult.store(3);
					return;
				}
				VARIANT name{};
				name.vt = VT_BSTR;
				name.bstrVal = SysAllocString(L"Run");
				if (!name.bstrVal)
				{
					clientResult.store(4);
					return;
				}
				ComPtr<IUIAutomationCondition> condition;
				const HRESULT conditionResult = automation->CreatePropertyCondition(UIA_NamePropertyId, name, &condition);
				VariantClear(&name);
				ComPtr<IUIAutomationElement> element;
				if (FAILED(conditionResult) || FAILED(rootElement->FindFirst(TreeScope_Descendants, condition.Get(), &element)) || !element)
				{
					clientResult.store(5);
					return;
				}
				ComPtr<IUIAutomationInvokePattern> invoke;
				if (FAILED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) || !invoke || FAILED(invoke->Invoke()))
				{
					clientResult.store(6);
				}
			});
			bool bReceived = false;
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			while ((!bDone.load() || !bReceived) && rox::core::MonotonicClock::now() < deadline)
			{
				(void)system.PollEvents();
				core::Result<os::AccessibilityRequest> result = system.PollAccessibilityAction(window);
				if (!result)
				{
					ADD_FAILURE() << "Could not poll accessibility request";
					break;
				}
				if (result.Value().IsValid())
				{
					os::AccessibilityRequest request = core::Move(result.Value());
					if (request.GetAction().Kind == os::EAccessibilityAction::Invoke)
					{
						bReceived = true;
						os::AccessibilityRequest copied = request;
						os::AccessibilityRequest moved = core::Move(copied);
						EXPECT_FALSE(copied.IsValid());
						EXPECT_TRUE(moved.IsValid());
						EXPECT_EQ(moved.GetAction().Target, (os::AccessibilityId{ 1, 1, 1, 1 }));
						EXPECT_EQ(moved.GetAction().Publication, 42);
						EXPECT_TRUE(request.TryBegin());
						EXPECT_FALSE(moved.TryBegin());
						moved.Complete(core::EErrorCode::None);
						EXPECT_FALSE(request.TryBegin());
					}
					else if (request.TryBegin())
					{
						request.Complete(core::EErrorCode::None);
					}
				}
				if (bDone.load() && clientResult.load() != 0)
				{
					break;
				}
				rox::core::SleepFor(rox::core::Milliseconds(1));
			}
			if (!bDone.load())
			{
				(void)system.DestroyWindow(window);
				bWindowAlive = false;
			}
			client.join();
			EXPECT_EQ(clientResult.load(), 0) << "UIA HRESULT: " << std::hex << static_cast<unsigned long>(clientStatus.load());
			EXPECT_TRUE(bReceived);
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}
}
