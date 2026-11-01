#pragma once

#include <ByteOperations.hpp>
#include <Cancellation.h>
#include <FailingMemoryResource.h>
#include <Shader.h>
#include <ShaderBuilder.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <Utility.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace rox::shader::test
{
	inline core::Result<core::Array<core::Byte>> readFixture(const char* name, core::MemoryResource& memory)
	{
		std::ifstream file(std::string(SHADER_TEST_DATA) + "/Compiled/" + name + ".shader", std::ios::binary | std::ios::ate);
		if (!file || file.tellg() <= 0)
		{
			return core::Result<core::Array<core::Byte>>::Failure(core::EErrorCode::NotFound, "Shader.Test.Fixture", "Missing fixture.");
		}
		const core::usize size = static_cast<core::usize>(file.tellg());
		core::Array<core::Byte> bytes(memory);
		if (!bytes.ResizeUninitialized(size))
		{
			return core::Result<core::Array<core::Byte>>::Failure(core::EErrorCode::ExternalFailure, "Shader.Test.Fixture", "Fixture allocation failed.");
		}
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.Data()), size);
		if (!file)
		{
			return core::Result<core::Array<core::Byte>>::Failure(core::EErrorCode::ExternalFailure, "Shader.Test.Fixture", "Fixture read failed.");
		}
		return core::Result<core::Array<core::Byte>>::Success(core::Move(bytes));
	}

	inline core::Result<Shader> loadFixture(const char* name, core::MemoryResource& memory)
	{
		core::Result<core::Array<core::Byte>> bytes = readFixture(name, memory);
		if (!bytes)
		{
			return core::Result<Shader>::Failure(bytes.GetError());
		}
		return Shader::Load(bytes.Value().View(), memory);
	}

	template <class T>
	void expectError(const core::Result<T>& result, core::EErrorCode code)
	{
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, code);
		EXPECT_STREQ(result.GetError().DiagnosticID.CStr(), "ROX-SHADER-ARTIFACT");
		EXPECT_FALSE(result.GetError().Message.IsEmpty());
	}

	inline void expectBytes(core::Span<const core::Byte> actual, core::Span<const core::Byte> expected)
	{
		ASSERT_EQ(actual.Size(), expected.Size());
		EXPECT_EQ(core::CompareBytes(actual.Data(), expected.Data(), actual.Size()), 0);
	}

	class ShaderTest : public testing::Test
	{
	protected:
		void TearDown() override
		{
			EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0u);
		}

		core::TrackingMemoryResource m_Memory{core::SystemMemoryResource::Get()};
	};

	inline ShaderTargetProfile profile(EShaderTarget target)
	{
		ShaderTargetProfile value;
		value.Target = target;
		return value;
	}
}
