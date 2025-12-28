cbuffer XForm : register(b0, space1)
{
    float4x4 modelXform;
    float4x4 modelViewProjXform;
};

struct Material
{
    float4 color;
};

cbuffer MaterialBlock : register(b1, space1)
{
    Material materials[16];
};

cbuffer MaterialBlock : register(b2, space1)
{
    int materialIndex;
};

struct VSInput
{
    float3 inPosition : TEXCOORD0;
    float3 inNormal : TEXCOORD1;
    float2 inTexCoord : TEXCOORD2;
};
struct VSOutput
{
    float4 position : SV_POSITION;
    float4 fragColor : COLOR;
    float3 fragNormal : NORMAL;
    float2 texCoord : TEXCOORD0;
};
VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(modelViewProjXform, float4(input.inPosition, 1.0));
    output.fragColor = materials[materialIndex].color;
    output.fragNormal = normalize(mul(modelXform, float4(input.inNormal, 0.0)).xyz);
    output.texCoord = input.inTexCoord;
    return output;
};