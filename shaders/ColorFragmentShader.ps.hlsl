struct PSInput
{
    float4 position : SV_POSITION;
    float4 fragColor : COLOR;
    float3 fragNormal : NORMAL;
};

// All fragments use the same texture/sampler layout
Texture2D<float4> texture : register(t0, space2);
SamplerState textureSampler : register(s0, space2);

float4 main(PSInput input) : SV_TARGET
{
    float3 lightDir = normalize(float3(1, -1, 1));
    float ambientFactor = 0.1;
    float diff = max(-dot(input.fragNormal, lightDir), 0.0);
    float3 diffuse = diff * input.fragColor.rgb;
    float3 ambient = ambientFactor * input.fragColor.rgb;
    return float4(clamp(diffuse + ambient, 0.0, 1.0), input.fragColor.a);
};