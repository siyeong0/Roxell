// Authored scene adapters. These inputs do not reconstruct captured DS2 producers.
struct PostPrepareParameters
{
    float4 Extent;
    float4 Exposure;
};
ConstantBuffer<PostPrepareParameters> Parameters : register(b0);
Texture2D<float3> SceneColor : register(t1);
Texture2D<float3> SeparateSpecular : register(t2);
RWTexture2D<float3> LinearHdr : register(u3);
RWTexture2D<float3> ExposedHdr : register(u4);
RWTexture2D<float> Scalar : register(u5);

[shader("compute")]
[numthreads(8,8,1)]
void PrepareExposure(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(Parameters.Extent.xy))) return;
    float3 linear = max(SceneColor.Load(int3(id.xy,0)) + SeparateSpecular.Load(int3(id.xy,0)), 0.0);
    LinearHdr[id.xy] = linear;
    ExposedHdr[id.xy] = linear * Parameters.Exposure.x;
    if (all(id.xy == 0)) Scalar[uint2(0,0)] = 1.0;
}

// Final Weather radiance already includes the separate environment specular term.
[shader("compute")]
[numthreads(8,8,1)]
void PrepareComposedExposure(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(Parameters.Extent.xy))) return;
    float3 linear = max(SceneColor.Load(int3(id.xy,0)),0.0);
    LinearHdr[id.xy] = linear;
    ExposedHdr[id.xy] = linear * Parameters.Exposure.x;
    if (all(id.xy == 0)) Scalar[uint2(0,0)] = 1.0;
}

// Independent entry resources use separate spaces to retain unique reflection records.
Texture2D<float3> PackedScene : register(t1,space1);
RWTexture2D<float4> LuminanceTiles : register(u2,space1);
RWTexture2D<float3> ColorTiles : register(u3,space1);
RWTexture2D<float4> Miniature : register(u4,space1);
SamplerState LinearClamp : register(s5,space1);

float3 AverageCell(uint2 cell, float2 extent)
{
    float3 total = 0.0;
    [unroll] for (uint y=0; y<4; ++y)
        [unroll] for (uint x=0; x<4; ++x)
            total += max(PackedScene.SampleLevel(LinearClamp,
                (float2(cell) + (float2(x,y)+0.5)*0.25) / extent, 0.0), 0.0);
    return total * (Parameters.Exposure.y * 0.0625);
}

[shader("compute")]
[numthreads(8,8,1)]
void PrepareLuminance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 32 || id.y >= 256) return;
    if (id.y < 18)
    {
        float3 color = AverageCell(id.xy, float2(32,18));
        float luminance = max(dot(color,float3(0.2126,0.7152,0.0722)),1.0e-6);
        LuminanceTiles[id.xy] = float4(sqrt(luminance),pow(luminance,1.0/4.8),sqrt(luminance),1.0);
        ColorTiles[id.xy] = color;
    }
    // The reduction consumes rows 248..255; repeat eight stratified scene rows.
    Miniature[id.xy] = float4(AverageCell(uint2(id.x,(255-id.y) & 7),float2(32,8)),1.0);
}

Texture2D<float4> Source : register(t1,space2);
RWTexture2D<float3> Output : register(u2,space2);
SamplerState SourceSampler : register(s3,space2);

[shader("compute")]
[numthreads(8,8,1)]
void CopyColor(uint3 id : SV_DispatchThreadID)
{
    uint width,height;
    Output.GetDimensions(width,height);
    if (id.x >= width || id.y >= height) return;
    Output[id.xy] = Source.SampleLevel(SourceSampler,(float2(id.xy)+0.5)/float2(width,height),0.0).rgb;
}
