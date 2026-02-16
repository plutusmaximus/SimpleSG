// Fullscreen triangle. Samples src texture at t0 with sampler s0.

Texture2D<float4> g_Src    : register(t0, space2);
SamplerState      g_Sample : register(s0, space2);

struct VSOut
{
    float4 Pos : SV_Position;
    float2 UV  : TEXCOORD0;
};

float4 main(VSOut i) : SV_Target0
{
    return g_Src.Sample(g_Sample, i.UV);
}
