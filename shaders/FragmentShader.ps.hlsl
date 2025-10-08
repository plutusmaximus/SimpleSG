struct PSInput
{
    float4 position : SV_POSITION;
    float4 fragColor : COLOR;
    float3 fragNormal : NORMAL;
    float2 texCoord : TEXCOORD0;
};

Texture2D<float4> texture : register(t0, space2);
SamplerState textureSampler : register(s0, space2);

float4 main(PSInput input) : SV_TARGET
{
    float3 lightDir = normalize(float3(0, 0, 1));
    float diff = max(-dot(input.fragNormal, lightDir), 0.0);
    float4 diffuse = diff * input.fragColor;
    return diffuse * texture.Sample(textureSampler, input.texCoord);
};