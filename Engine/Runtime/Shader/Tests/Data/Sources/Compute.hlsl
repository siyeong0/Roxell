[[vk::binding(0, 0)]] cbuffer Settings : register(b0) { uint Offset; uint Scale; };
[[vk::binding(1, 0)]] StructuredBuffer<uint> Input : register(t0);
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> Output : register(u0);
[numthreads(8, 2, 1)]
void Main(uint3 id : SV_DispatchThreadID) { Output[id.x] = Input[id.x] * Scale + Offset; }
[numthreads(8, 2, 1)]
void Fast(uint3 id : SV_DispatchThreadID) { Output[id.x] = Input[id.x] + Offset; }
