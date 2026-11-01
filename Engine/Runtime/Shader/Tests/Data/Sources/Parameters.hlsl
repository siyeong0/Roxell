struct Nested { float4 Color; };
[[vk::binding(0, 0)]] cbuffer Settings : register(b0)
{
    float4 Tint;
    float Exposure;
    float Weights[2];
    row_major float2x2 Transform;
    Nested Details;
};
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Output : register(u0);
[numthreads(1, 1, 1)]
void Main(uint3 id : SV_DispatchThreadID)
{
    float2 transformed = mul(Transform, float2(Weights[0], Weights[1]));
    Output[id.x] = Tint * Exposure + Details.Color + float4(transformed, 0, 0);
}
