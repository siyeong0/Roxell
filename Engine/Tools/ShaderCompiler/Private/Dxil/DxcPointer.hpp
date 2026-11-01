#pragma once

#include "DxcSdk.h"

#include <Assertion.h>

#if defined(_WIN32)
namespace rox::shader
{
	template <typename T> class DxcPointer final
	{
	public:
		~DxcPointer()
		{
			if (m_Value != nullptr)
			{
				m_Value->Release();
			}
		}

		DxcPointer() = default;
		DxcPointer(const DxcPointer&) = delete;
		DxcPointer& operator=(const DxcPointer&) = delete;

		T** Put()
		{
			ASSERT(m_Value == nullptr, "DXC output storage must be empty.");
			return &m_Value;
		}

		T* Get() const
		{
			return m_Value;
		}

		T* operator->() const
		{
			ASSERT(m_Value != nullptr, "DXC interface must exist before use.");
			return m_Value;
		}

	private:
		T* m_Value = nullptr;
	};

} // namespace rox::shader
#endif
