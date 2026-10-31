#include <ITraceSink.h>

#include <gtest/gtest.h>
#include <memory>

using namespace rox::core;

namespace
{
	class DestructionSink final : public ITraceSink
	{
	public:
		explicit DestructionSink(bool& bDestroyed) noexcept : m_bDestroyed(bDestroyed) {}
		~DestructionSink() override
		{
			m_bDestroyed = true;
		}
		void Write(const TraceEvent&) noexcept override {}

	private:
		bool& m_bDestroyed;
	};
}

TEST(ITraceSinkTests, DestructionThroughBaseReleasesDerivedSink)
{
	bool bDestroyed = false;
	{
		std::unique_ptr<ITraceSink> sink = std::make_unique<DestructionSink>(bDestroyed);
		EXPECT_FALSE(bDestroyed);
	}
	EXPECT_TRUE(bDestroyed);
}
