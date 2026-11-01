// EIDs 43513/43537 / CS16030/16169: 1,024 cosine-weighted dome samples
// per octahedral skylight direction. Global averages are a separate reduction.
struct DomeParameters { float4 Registers[6]; };
[[vk::binding(0,0)]] ConstantBuffer<DomeParameters> Parameters : register(b0);
[[vk::binding(1,0)]] Texture2D<float4> Dome : register(t1);
[[vk::binding(2,0)]] Texture2D<float4> PreviousSkyLight : register(t2);
[[vk::binding(3,0)]] SamplerState DomeSampler : register(s3);
[[vk::binding(4,0)]] RWTexture2D<float4> SkyLight : register(u4);
#if defined(ROXELL_STRATA_MANAGED_BUFFERS)
[[vk::binding(5,0)]] RWStructuredBuffer<uint> Completion : register(u5);
#else
[[vk::binding(5,0)]] RWBuffer<uint> Completion : register(u5);
#endif
groupshared float4 Partial[64];

float2 DomeUv(float3 direction)
{
    float angle = atan(direction.x / direction.y);
    if (direction.y < 0.0 && direction.x >= 0.0) angle += 3.1415927410125732;
    if (direction.y < 0.0 && direction.x < 0.0) angle -= 3.1415927410125732;
    float u = angle * 0.15915493667125702 + 0.5;
    if (direction.y == 0.0 && direction.x < 0.0) u = 0.25;
    if (direction.y == 0.0 && direction.x >= 0.0) u = 0.75;
    if (isnan(u)) u = 0.0;
    return float2(u, 1.0 - abs(direction.z));
}

float4 SampleHemisphere(uint2 group, uint2 thread)
{
    float2 plane = float2(group) * 0.13333334028720856 - 1.0;
    float2 rotated = float2(plane.y + plane.x, plane.x - plane.y) * 0.5;
    float3 normal = normalize(float3(rotated, 1.0 - dot(abs(rotated), float2(1.0))));
    float3 tangent = normalize(cross(normal, abs(normal.x) > 0.9 ? float3(0,1,0) : float3(1,0,0)));
    float3 bitangent = cross(normal, tangent);
    float4 sum = 0.0;
    [loop]
    for (uint x = 0; x < 4; ++x)
    {
        [loop]
        for (uint y = 0; y < 4; ++y)
        {
            float2 sample = frac((float2(thread * 4 + uint2(x,y)) + 0.5) * 0.03125);
            float azimuth = sample.x * 6.2831854820251465;
            float polar = sample.y * 1.5707963705062866;
            float sine = sin(polar);
            float cosine = cos(polar);
            float3 local = float3(sine * cos(azimuth), sin(azimuth) * sine, cosine);
            // Preserve the recovered tangent/bitangent sum before adding the normal term.
            float3 direction = normalize(mad(local.z, normal, mad(local.y, tangent, local.x * bitangent)));
            float3 color = Dome.SampleLevel(DomeSampler, DomeUv(direction), 0.0).rgb;
            sum = float4(color * cosine + sum.rgb, cosine + sum.a);
        }
    }
    return sum;
}

void WriteDirection(bool update, uint2 group, uint2 thread, uint lane)
{
    Partial[lane] = SampleHemisphere(group, thread);
    GroupMemoryBarrierWithGroupSync();
    [loop]
    for (uint stride = 32; stride > 0; stride >>= 1)
    {
        if (lane < stride) Partial[lane] = Partial[lane + stride] + Partial[lane];
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0)
    {
        float3 average = Partial[0].a != 0.0 ? Partial[0].rgb / Partial[0].a : float3(0.0);
        float3 exposed = average / Parameters.Registers[0].x;
        float luminance = dot(exposed, float3(0.2125999927520752,0.7152000069618225,0.0722000002861023));
        float3 adjusted = (exposed + (luminance - exposed) * Parameters.Registers[0].z) * Parameters.Registers[0].y;
        if (update)
        {
            float3 previous = PreviousSkyLight.Load(int3(group + uint2(0,20),0)).rgb;
            float3 temporal = previous + (adjusted - previous) * Parameters.Registers[0].w;
            SkyLight[group + uint2(0,20)] = float4(temporal,0.0);
            float gray = dot(temporal, float3(0.2125999927520752,0.7152000069618225,0.0722000002861023));
            float3 tinted = (temporal + (gray - temporal) * Parameters.Registers[2].x) * Parameters.Registers[2].yzw;
            SkyLight[group + uint2(0,36)] = float4(temporal + (tinted - temporal) * Parameters.Registers[1].x,0.0);
        }
        else
        {
            SkyLight[group + uint2(0,4)] = float4(adjusted,0.0);
            SkyLight[group + uint2(0,20)] = float4(adjusted,0.0);
            SkyLight[group + uint2(0,36)] = float4(adjusted,0.0);
        }
        uint previousCount;
        InterlockedAdd(Completion[asuint(Parameters.Registers[3].x)],1,previousCount);
    }
}

[shader("compute")]
[numthreads(8,8,1)]
void InitializeMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint lane : SV_GroupIndex)
{
    WriteDirection(false,group.xy,thread.xy,lane);
}

[shader("compute")]
[numthreads(8,8,1)]
void UpdateMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint lane : SV_GroupIndex)
{
    WriteDirection(true,group.xy,thread.xy,lane);
}
