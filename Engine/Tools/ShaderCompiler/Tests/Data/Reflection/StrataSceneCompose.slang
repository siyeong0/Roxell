// Authored adapters between actual geometry, lighting and atmospheric producers.
struct SceneComposeParameters
{
    row_major float4x4 InverseProjection;
    uint2 Extent;
    float DepthCutoff;
    float Reserved;
};
ConstantBuffer<SceneComposeParameters> Parameters : register(b0);
Texture2D<float> DeviceDepth : register(t1);
RWTexture2D<float> LinearDepth : register(u2);

[shader("compute")]
[numthreads(8,8,1)]
void LinearizeDepth(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Parameters.Extent)) return;
    float depth = DeviceDepth.Load(int3(id.xy,0));
    float2 uv = (float2(id.xy) + 0.5) / float2(Parameters.Extent);
    float4 view = mul(float4(uv * float2(2,-2) + float2(-1,1),depth,1), Parameters.InverseProjection);
    // Reverse-depth background is an explicit finite scene-distance cutoff.
    float distance = depth > 0 && view.w != 0 ? -view.z / view.w : Parameters.DepthCutoff;
    LinearDepth[id.xy] = clamp(distance,0.0,Parameters.DepthCutoff);
}

Texture2D<float3> SceneColor : register(t1,space1);
Texture2D<float3> SeparateSpecular : register(t2,space1);
RWTexture2D<float3> ComposedHdr : register(u3,space1);

[shader("compute")]
[numthreads(8,8,1)]
void ComposeHdr(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Parameters.Extent)) return;
    ComposedHdr[id.xy] = max(SceneColor.Load(int3(id.xy,0)) + SeparateSpecular.Load(int3(id.xy,0)),0.0);
}
