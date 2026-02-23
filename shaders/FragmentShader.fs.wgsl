struct PSInput
{
    @builtin(position) position: vec4<f32>,
    @location(0) fragNormal: vec3<f32>,
    @location(1) texCoord: vec2<f32>,
};

struct Material
{
    color : vec4<f32>,
    roughness : f32,
    metallic : f32,
    pad0 : f32,
    pad1 : f32
};

@group(2) @binding(0) var texture0: texture_2d<f32>;
@group(2) @binding(1) var textureSampler: sampler;
@group(2) @binding(2) var<uniform> material : Material;

@fragment
fn main(input: PSInput) -> @location(0) vec4<f32>
{
    let lightDir = normalize(vec3<f32>(1.0, -1.0, 1.0));
    let ambientFactor = 0.1;
    let diff = max(-dot(input.fragNormal, lightDir), 0.0);
    let diffuse = diff * material.color.rgb;
    let ambient = ambientFactor * material.color.rgb;
    let color = clamp(diffuse + ambient, vec3<f32>(0.0), vec3<f32>(1.0));
    let litColor = vec4<f32>(color, material.color.a);
    return litColor * textureSample(texture0, textureSampler, input.texCoord);
}
