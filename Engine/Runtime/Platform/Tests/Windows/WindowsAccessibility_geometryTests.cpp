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

	class RectangleGeometry final : public os::IAccessibilityTextGeometry
	{
	public:
		explicit RectangleGeometry(rox::core::Atomic<bool>* queried) noexcept
			: m_Queried(queried)
		{
		}

		core::Result<core::Array<os::AccessibilityRect>> GetBounds(core::uint32 first, core::uint32 last, core::MemoryResource& memory) const override
		{
			m_Queried->store(first == 0 && last == 3);
			core::Array<os::AccessibilityRect> bounds(memory);
			if (!bounds.Add({ 7, 9, 30, 20 }))
			{
				return core::Result<core::Array<os::AccessibilityRect>>::Failure(core::EErrorCode::ExternalFailure, "Test.Geometry", "Allocation failed.");
			}
			return core::Result<core::Array<os::AccessibilityRect>>::Success(core::Move(bounds));
		}

		core::Result<core::uint32> HitTest(core::float64, core::float64) const override
		{
			return core::Result<core::uint32>::Success(0);
		}

		core::uint32 NextBoundary(core::uint32 byte, bool) const noexcept override
		{
			return byte;
		}

	private:
		rox::core::Atomic<bool>* m_Queried;
	};

	TEST(WindowsAccessibilityGeometryTests, TextRangeConvertsClientRectanglesToScreenCoordinates)
	{
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		rox::core::Atomic<bool> bQueried{ false };
		{
			os::WindowsWindowSystem system;
			ASSERT_TRUE(os::WindowsWindowSystem::TryCreate(memory, {}, &system));
			os::WindowDesc desc;
			desc.Title = "Roxell accessibility geometry test";
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
			POINT origin{};
			ASSERT_NE(ClientToScreen(handle, &origin), FALSE);
			core::Array<os::AccessibilityNode> nodes(memory);
			os::AccessibilityNode root(memory);
			root.Id = { 1, 1, 1, 0 };
			nodes.AddRequired(core::Move(root));
			os::AccessibilityNode edit(memory);
			edit.Id = { 1, 1, 1, 1 };
			edit.Parent = { 1, 1, 1, 0 };
			edit.Role = os::EAccessibilityRole::Edit;
			edit.ContentRevision = 1;
			ASSERT_TRUE(edit.Name.Assign("Document"));
			ASSERT_TRUE(edit.Value.Assign("abc"));
			edit.Geometry = core::InterfaceOwner<os::IAccessibilityTextGeometry>::Create<RectangleGeometry>(memory, &bQueried);
			ASSERT_TRUE(edit.Geometry);
			nodes.AddRequired(core::Move(edit));
			core::Result<os::AccessibilitySnapshot> snapshot = os::AccessibilitySnapshot::Create(1, core::Move(nodes));
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
				ComPtr<IUIAutomationElement> rootElement;
				const HRESULT automationStatus = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
				if (FAILED(automationStatus))
				{
					clientStatus.store(automationStatus);
					clientResult.store(2);
					return;
				}
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
					clientResult.store(2);
					return;
				}
				VARIANT name{};
				name.vt = VT_BSTR;
				name.bstrVal = SysAllocString(L"Document");
				if (!name.bstrVal)
				{
					clientResult.store(3);
					return;
				}
				ComPtr<IUIAutomationCondition> condition;
				const HRESULT conditionResult = automation->CreatePropertyCondition(UIA_NamePropertyId, name, &condition);
				VariantClear(&name);
				ComPtr<IUIAutomationElement> element;
				if (FAILED(conditionResult) || FAILED(rootElement->FindFirst(TreeScope_Descendants, condition.Get(), &element)) || !element)
				{
					clientResult.store(4);
					return;
				}
				ComPtr<IUIAutomationTextPattern> text;
				ComPtr<IUIAutomationTextRange> range;
				if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) || !text ||
					FAILED(text->get_DocumentRange(&range)) || !range)
				{
					clientResult.store(5);
					return;
				}
				SAFEARRAY* rectangles = nullptr;
				if (FAILED(range->GetBoundingRectangles(&rectangles)) || !rectangles)
				{
					clientResult.store(6);
					return;
				}
				core::ScopeExit destroyArray([&]() noexcept { SafeArrayDestroy(rectangles); });
				LONG upper = -1;
				if (FAILED(SafeArrayGetUBound(rectangles, 1, &upper)) || upper != 3)
				{
					clientResult.store(7);
					return;
				}
				double values[4]{};
				for (LONG index = 0; index < 4; ++index)
				{
					if (FAILED(SafeArrayGetElement(rectangles, &index, &values[index])))
					{
						clientResult.store(8);
						return;
					}
				}
				if (values[0] != origin.x + 7 || values[1] != origin.y + 9 || values[2] != 30 || values[3] != 20)
				{
					clientResult.store(9);
				}
			});
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			while (!bDone.load() && rox::core::MonotonicClock::now() < deadline)
			{
				(void)system.PollEvents();
				rox::core::SleepFor(rox::core::Milliseconds(1));
			}
			if (!bDone.load())
			{
				(void)system.DestroyWindow(window);
				bWindowAlive = false;
			}
			client.join();
			EXPECT_EQ(clientResult.load(), 0) << "UIA HRESULT: " << std::hex << static_cast<unsigned long>(clientStatus.load());
			EXPECT_TRUE(bQueried.load());
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}
}
