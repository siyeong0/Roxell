#include "Constants.hlsli"

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> Output : register(u0);

[numthreads(8, 2, 1)]
void Main(uint3 id : SV_DispatchThreadID)
{
    Output[id.x] = id.x + TEST_OFFSET;
}
