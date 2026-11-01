#include <Rhi.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(NullDeviceDeathTest, RayShaderOwnership)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			Result<void> created = CreateDevice(EBackend::Null, memory, &device);
			ASSERT_TRUE(created);
			const Byte bytecode[4]{ static_cast<Byte>(1), static_cast<Byte>(2), static_cast<Byte>(3), static_cast<Byte>(4) };
			const EShaderStage stages[6]{ EShaderStage::RayGeneration, EShaderStage::Miss, EShaderStage::ClosestHit, EShaderStage::AnyHit, EShaderStage::Intersection, EShaderStage::Callable };
			ShaderLibraryEntryDesc entries[6]{};
			for (usize index = 0; index < 6; ++index)
			{
				entries[index] = { stages[index], bytecode, "RayMain" };
			}

			EXPECT_DEATH((void)(device->CreateShader({ static_cast<EShaderStage>(255), bytecode })), "The shader stage, bytecode, or entry point is invalid.");
			for (const EShaderStage stage : stages)
			{
				Result<ShaderHandle> shader = device->CreateShader({ stage, bytecode, "RayMain" });
				ASSERT_TRUE(shader);
				ASSERT_TRUE(device->DestroyShader(shader.Value()));
			}

			EXPECT_DEATH((void)(device->CreateRayTracingPipeline({})), "The ray pipeline description is invalid.");
			Result<ShaderHandle> rayGeneration = device->CreateShader({ EShaderStage::RayGeneration, bytecode, "RayMain" });
			ASSERT_TRUE(rayGeneration);
			const RayTracingShaderDesc rayEntry{ rayGeneration.Value() };
			RayTracingPipelineDesc rayDesc{};
			rayDesc.Shaders = { &rayEntry, 1 };
			Result<RayTracingPipelineHandle> unsupportedPipeline = device->CreateRayTracingPipeline(rayDesc);
			ASSERT_TRUE(!unsupportedPipeline && unsupportedPipeline.GetError().Code == EErrorCode::Unsupported);
			ASSERT_TRUE(device->DestroyShader(rayGeneration.Value()));
			EXPECT_DEATH(([&] { (void)(device->DestroyRayTracingPipeline({})); })(), "handle");
			EXPECT_DEATH(([&] { (void)(device->CreateShaderTable({})); })(), "handle");
			EXPECT_DEATH(([&] { (void)(device->DestroyShaderTable({})); })(), "handle");
			EXPECT_DEATH(([&] { (void)(device->DispatchRays({}, RayBindings{}, {})); })(), "handle");
			Result<ShaderLibraryHandle> library = device->CreateShaderLibrary(entries);
			ASSERT_TRUE(library);

			for (const EShaderStage stage : stages)
			{
				Result<ShaderHandle> shader = device->AcquireShader(library.Value(), stage, "RayMain");
				ASSERT_TRUE(shader);
				ASSERT_FALSE(device->DestroyShaderLibrary(library.Value()));
				ASSERT_TRUE(device->DestroyShader(shader.Value()));
			}

			EXPECT_DEATH((void)(device->AcquireShader(library.Value(), static_cast<EShaderStage>(255), "RayMain")), "The shader entry query is invalid.");
			ASSERT_TRUE(device->DestroyShaderLibrary(library.Value()));
		}

		ASSERT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(NullDeviceTest, MissingShaderEntryIsRecoverableAndDoesNotRetainTheLibrary)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			UniquePtr<Device> device;
			ASSERT_TRUE(CreateDevice(EBackend::Null, memory, &device));
			const Byte bytes[]{ Byte{ 1 }, Byte{ 2 }, Byte{ 3 }, Byte{ 4 } };
			const ShaderLibraryEntryDesc entry{ EShaderStage::Compute, bytes, "Kernel" };
			const Result<ShaderLibraryHandle> library = device->CreateShaderLibrary({ &entry, 1 });
			ASSERT_TRUE(library);
			const Result<ShaderHandle> missingName = device->AcquireShader(library.Value(), EShaderStage::Compute, "Missing");
			ASSERT_FALSE(missingName);
			EXPECT_EQ(missingName.GetError().Code, EErrorCode::NotFound);
			const Result<ShaderHandle> missingStage = device->AcquireShader(library.Value(), EShaderStage::Vertex, "Kernel");
			ASSERT_FALSE(missingStage);
			EXPECT_EQ(missingStage.GetError().Code, EErrorCode::NotFound);
			EXPECT_TRUE(device->DestroyShaderLibrary(library.Value()));
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
	}
}
