cbuffer ClearConstants : register(b0)
{
    uint4 ClearBits;
};

float4 ClearVertex(uint vertex : SV_VertexID) : SV_Position
{
    float2 position = float2((vertex << 1) & 2, vertex & 2);
    return float4(position * float2(2, -2) + float2(-1, 1), 0, 1);
}

uint4 ClearUnsigned() : SV_Target0
{
    return ClearBits;
}

int4 ClearSigned() : SV_Target0
{
    return asint(ClearBits);
}
