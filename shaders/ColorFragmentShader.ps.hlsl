struct PSInput
{
    float4 position : SV_POSITION;
    float4 fragColor : COLOR;
    float3 fragNormal : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 lightDir = normalize(float3(1, -1, 1));
    float ambientFactor = 0.1;
    float diff = max(-dot(input.fragNormal, lightDir), 0.0);
    float3 diffuse = diff * input.fragColor.rgb;
    float3 ambient = ambientFactor * input.fragColor.rgb;
    return float4(clamp(diffuse + ambient, 0.0, 1.0), input.fragColor.a);
};