// Independently reconstructed from frame11145 shader14426. Native validation is pending.
struct HeightParameters { float2 TileOffset; float BorderTexels; };
struct WorldRows { uint4 Rows[1185]; };
#if defined(ROXELL_STRATA_MANAGED_BUFFERS)
[[vk::binding(0, 0)]] ConstantBuffer<HeightParameters> Parameters : register(b0, space0);
[[vk::binding(1, 0)]] ConstantBuffer<WorldRows> World : register(b1, space0);
[[vk::binding(2, 0)]] RWTexture2D<float> Output : register(u2, space0);
[[vk::binding(0, 1)]] Texture2D<float4> TileTexturesLow[3720] : register(t0, space1);
[[vk::binding(1, 1)]] Texture2D<float4> TileTexturesHigh[3720] : register(t3720, space1);
[[vk::binding(0, 2)]] SamplerState HeightSampler : register(s0, space2);
[[vk::binding(1, 2)]] SamplerState MaskSampler : register(s1, space2);
#else
ConstantBuffer<HeightParameters> Parameters : register(b0, space0);
ConstantBuffer<WorldRows> World : register(b1, space0);
Texture2D<float4> TileTextures[7440] : register(t2, space0);
SamplerState HeightSampler : register(s7442, space0);
SamplerState MaskSampler : register(s7443, space0);
RWTexture2D<float> Output : register(u7444, space0);
#endif

#if defined(ROXELL_STRATA_MANAGED_BUFFERS)
void TileDimensions(uint index, out uint width, out uint height)
{
    if (index < 3720) TileTexturesLow[NonUniformResourceIndex(index)].GetDimensions(width, height);
    else TileTexturesHigh[NonUniformResourceIndex(index - 3720)].GetDimensions(width, height);
}

float4 SampleTile(uint index, SamplerState sourceSampler, float2 uv)
{
    if (index < 3720) return TileTexturesLow[NonUniformResourceIndex(index)].SampleLevel(sourceSampler, uv, 0.0);
    return TileTexturesHigh[NonUniformResourceIndex(index - 3720)].SampleLevel(sourceSampler, uv, 0.0);
}

#else
#endif
#if defined(ROXELL_STRATA_TERRAIN_COVERAGE)
#if !defined(ROXELL_STRATA_MANAGED_BUFFERS)
#error Explicit terrain coverage requires the managed resource layout.
#endif
[[vk::binding(3, 0)]] RWTexture2D<uint> OutputCoverage : register(u3, space0);
float LoadTileCoverage(uint index, int2 pixel)
{
    if (index < 3720) return TileTexturesLow[NonUniformResourceIndex(index)].Load(int3(pixel, 0)).r;
    return TileTexturesHigh[NonUniformResourceIndex(index - 3720)].Load(int3(pixel, 0)).r;
}
bool TileSupport(uint index, float2 uv)
{
    uint width, height;
    TileDimensions(index, width, height);
    if (width <= 4 && height <= 4) return false;
    int2 cell = int2(floor(uv * float2(width, height) - 0.5));
    if (any(cell < 0) || any(cell + 1 >= int2(width, height))) return false;
    return LoadTileCoverage(index, cell) == 1.0 &&
        LoadTileCoverage(index, cell + int2(1, 0)) == 1.0 &&
        LoadTileCoverage(index, cell + int2(0, 1)) == 1.0 &&
        LoadTileCoverage(index, cell + int2(1, 1)) == 1.0;
}
#endif
float SelectChannel(float4 value, uint packing)
{
    uint channel = packing & 7u;
    if ((packing & 6u) == 6u || channel == 0) return value.r;
    if (channel == 1) return value.g;
    if (channel == 2) return value.b;
    return channel == 3 ? value.a : value.r;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 thread : SV_DispatchThreadID)
{
    uint width, height;
    Output.GetDimensions(width, height);
    float2 normalized = (float2(thread.xy) + 0.5) / float2(width, height);
    float2 world = normalized * 768.0 + (float2(World.Rows[0].xy) + Parameters.TileOffset) * 256.0;
    float2 tileCoordinate = world * 0.00390625;
    float2 uv = float2(frac(tileCoordinate.x), frac(1.0 - tileCoordinate.y));
    int2 grid = int2(World.Rows[0].zw);
#if defined(ROXELL_STRATA_MANAGED_BUFFERS)
    // Authored signed grid origin; the recovered no-define addressing remains unchanged.
    int2 tile = clamp(int2(floor(tileCoordinate)) - asint(World.Rows[1057].xy), int2(0), grid - 1);
#else
    int2 tile = min(max(int2(tileCoordinate), int2(0)), grid - 1);
#endif
    uint tileIndex = min(uint(tile.y * grid.x + tile.x), 4095u);
    uint tileId = World.Rows[(tileIndex >> 2) + 1][tileIndex & 3u];
    uint4 maskPacking = World.Rows[1056];
    uint4 heightPacking = World.Rows[1074];
    uint maskIndex = maskPacking.x + tileId * 31;
    uint heightIndex = heightPacking.x + tileId * 31;
    uint heightWidth, heightHeight;
#if defined(ROXELL_STRATA_MANAGED_BUFFERS)
    TileDimensions(heightIndex, heightWidth, heightHeight);
#else
    TileTextures[NonUniformResourceIndex(heightIndex)].GetDimensions(heightWidth, heightHeight);
#endif
    float border = Parameters.BorderTexels / float(heightWidth);
    float2 heightUv = (1.0 - border) * uv + border * 0.5;
    uint maskWidth, maskHeight;
#if defined(ROXELL_STRATA_MANAGED_BUFFERS)
    TileDimensions(maskIndex, maskWidth, maskHeight);
#else
    TileTextures[NonUniformResourceIndex(maskIndex)].GetDimensions(maskWidth, maskHeight);
#endif
    float mask = 0.0;
    if (maskWidth > 4 || maskHeight > 4)
    {
        float borderFraction = asfloat(maskPacking.w);
        float2 maskUv = (1.0 - borderFraction) * uv + borderFraction * 0.5;
#if defined(ROXELL_STRATA_MANAGED_BUFFERS)
        float4 sampled = SampleTile(maskIndex, MaskSampler, maskUv);
#else
        float4 sampled = TileTextures[NonUniformResourceIndex(maskIndex)].SampleLevel(MaskSampler, maskUv, 0.0);
#endif
        mask = SelectChannel(sampled, maskPacking.y);
    }
    float value = 0.0;
    if (heightWidth > 4 || heightHeight > 4)
    {
#if defined(ROXELL_STRATA_MANAGED_BUFFERS)
        float4 sampled = SampleTile(heightIndex, HeightSampler, heightUv);
#else
        float4 sampled = TileTextures[NonUniformResourceIndex(heightIndex)].SampleLevel(HeightSampler, heightUv, 0.0);
#endif
        value = SelectChannel(sampled, heightPacking.y);
    }
#if defined(ROXELL_STRATA_TERRAIN_COVERAGE)
    uint coverageIndex = tileId * 31 + 2;
    float maskBorder = asfloat(maskPacking.w);
    float2 supportUv = (1.0 - maskBorder) * uv + maskBorder * 0.5;
    int2 actualTile = int2(floor(tileCoordinate)) - asint(World.Rows[1057].xy);
    bool covered = all(actualTile >= 0) && all(actualTile < grid) &&
        TileSupport(coverageIndex, heightUv) && TileSupport(coverageIndex, supportUv);
    OutputCoverage[thread.xy] = covered ? 3u : 0u;
    Output[thread.xy] = covered ? max(mask, value) : 0.0;
#else
    Output[thread.xy] = max(mask, value);
#endif
}
