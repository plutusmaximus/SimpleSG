struct PSInput
{
    float4 position : SV_POSITION;
    float3 fragNormal : NORMAL;
    float2 texCoord : TEXCOORD0;
};

struct Material
{
    float4 color;
    float metalness;
    float roughness;
    float pad0;
    float pad1;
};

Texture2D<float4> texture : register(t0, space2);
SamplerState textureSampler : register(s0, space2);
ConstantBuffer<Material> material : register(b0, space3);

float4 main(PSInput input) : SV_TARGET
{
    float3 lightDir = normalize(float3(1, -1, 1));
    float ambientFactor = 0.1;
    float diff = max(-dot(input.fragNormal, lightDir), 0.0);
    float3 diffuse = diff * material.color.rgb;
    float3 ambient = ambientFactor * material.color.rgb;
    return float4(clamp(diffuse + ambient, 0.0, 1.0), material.color.a) * texture.Sample(textureSampler, input.texCoord);
};