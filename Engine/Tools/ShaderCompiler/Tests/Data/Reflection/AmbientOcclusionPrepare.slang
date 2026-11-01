// Producer equations recovered from EIDs 56921 (PS 14302) and 56939 (CS 18087).
struct AmbientOcclusionPrepareParameters
{
    float4 Rows[4];
};

[[vk::binding(0, 0)]] ConstantBuffer<AmbientOcclusionPrepareParameters> Parameters : register(b0);
[[vk::binding(1, 0)]] Texture2D<float> SceneDepth : register(t1);
[[vk::binding(2, 0)]] Texture2D<float2> SceneMotion : register(t2);
[[vk::binding(3, 0)]] Texture2D<float2> SceneNormal : register(t3);
[[vk::binding(4, 0)]] SamplerState SourceSampler : register(s4);
[[vk::binding(5, 0)]] RWTexture2D<float2> HalfMotion : register(u5);
[[vk::binding(6, 0)]] RWTexture2D<float2> HalfNormal : register(u6);
[[vk::binding(7, 0)]] RWTexture2D<float> ReverseDepthHistory : register(u7);

struct FullscreenVertex
{
    float4 Position : SV_Position;
    float2 Uv : TEXCOORD0;
};

FullscreenVertex VSMain(uint vertexID : SV_VertexID)
{
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    FullscreenVertex output;
    output.Position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    output.Uv = uv;
    return output;
}

struct DepthBounds
{
    float First : SV_Target0;
    float Second : SV_Target1;
};

float LinearDepth(float depth)
{
    float numerator = depth * Parameters.Rows[2].x + Parameters.Rows[2].y;
    float denominator = depth * Parameters.Rows[2].z + Parameters.Rows[2].w;
    float distance = -(numerator / denominator);
    return Parameters.Rows[3].y > distance ? min(distance, Parameters.Rows[3].x) : distance;
}

DepthBounds PSDepthBounds(FullscreenVertex input)
{
    float2 coordinate = (float2(int2(input.Position.xy)) + 0.5) * Parameters.Rows[1].xy;
    float2 low = Parameters.Rows[0].xy;
    float2 high = Parameters.Rows[0].zw;
    coordinate = max(min(coordinate, low), min(max(coordinate, low), high));
    float4 depths = SceneDepth.GatherRed(SourceSampler, coordinate);
    float minimum = min(depths.x, min(depths.y, min(depths.z, depths.w)));
    float maximum = max(depths.x, max(depths.y, max(depths.z, depths.w)));
    float adjustment = (maximum - minimum) * Parameters.Rows[1].z;
    DepthBounds output;
    output.First = LinearDepth(adjustment + minimum);
    output.Second = LinearDepth(maximum - adjustment);
    return output;
}

// EID 56912, PS 14499: reverse-depth gather reduction before the optional merge.
float PSHalfDepth(FullscreenVertex input) : SV_Depth
{
    float2 low = Parameters.Rows[0].xy;
    float2 high = Parameters.Rows[0].zw;
    float2 coordinate = max(min(input.Uv, low), min(max(input.Uv, low), high));
    float4 depths = SceneDepth.GatherRed(SourceSampler, coordinate);
    float minimum = min(depths.x, min(depths.y, min(depths.z, depths.w)));
    float maximum = max(depths.x, max(depths.y, max(depths.z, depths.w)));
    return maximum - (maximum - minimum) * Parameters.Rows[1].z;
}

// EID 56934, PS 17672; the caller supplies the captured Greater depth test.
float PSMergeDepth(FullscreenVertex input) : SV_Depth
{
    float depth;
    if (asuint(Parameters.Rows[2].z) != 0)
    {
        float4 depths = SceneDepth.GatherRed(SourceSampler, input.Uv);
        depth = asuint(Parameters.Rows[2].w) != 0
            ? min(depths.x, min(depths.y, min(depths.z, depths.w)))
            : max(depths.x, max(depths.y, max(depths.z, depths.w)));
    }
    else
    {
        depth = SceneDepth.Sample(SourceSampler, input.Uv);
    }
    if (Parameters.Rows[2].x != 0.0)
    {
        float linear = (depth * Parameters.Rows[0].x + Parameters.Rows[0].y) /
            (depth * Parameters.Rows[0].z + Parameters.Rows[0].w) + Parameters.Rows[2].x;
        return (linear * Parameters.Rows[1].x + Parameters.Rows[1].y) /
            (linear * Parameters.Rows[1].z + Parameters.Rows[1].w) + Parameters.Rows[2].y;
    }
    return depth + Parameters.Rows[2].y;
}

[numthreads(8, 8, 1)]
void CSNormalMotion(uint3 dispatchID : SV_DispatchThreadID)
{
    uint width;
    uint height;
    HalfMotion.GetDimensions(width, height);
    if (dispatchID.x >= width || dispatchID.y >= height)
        return;
    uint2 pixel = dispatchID.xy;
    float2 normal = SceneNormal.Load(int3(pixel * 2, 0));
    float2 uv = (float2(pixel) + 0.5) * Parameters.Rows[0].xy;
    HalfMotion[pixel] = SceneMotion.SampleLevel(SourceSampler, uv, 0.0);
    HalfNormal[pixel] = normal;
}

// Retain the current half-resolution device depth in the temporal input format.
[numthreads(8, 8, 1)]
void CSReverseDepth(uint3 dispatchID : SV_DispatchThreadID)
{
    uint width;
    uint height;
    ReverseDepthHistory.GetDimensions(width, height);
    if (dispatchID.x < width && dispatchID.y < height)
        ReverseDepthHistory[dispatchID.xy] = SceneDepth.Load(int3(dispatchID.xy, 0));
}
