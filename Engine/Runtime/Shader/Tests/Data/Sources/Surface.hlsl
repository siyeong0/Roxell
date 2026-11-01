[[vk::binding(0, 0)]] Texture2D<float4> Albedo : register(t0);
[[vk::binding(1, 0)]] SamplerState LinearSampler : register(s0);
struct Varyings { float4 Position : SV_Position; float2 UV : TEXCOORD0; };
Varyings Vertex(float2 position : POSITION) { Varyings v; v.Position = float4(position, 0, 1); v.UV = position * 0.5 + 0.5; return v; }
float4 Pixel(Varyings input) : SV_Target0 { return Albedo.Sample(LinearSampler, input.UV); }
