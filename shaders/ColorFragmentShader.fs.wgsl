struct PSInput
{
    @builtin(position) position: vec4<f32>,
    @location(0) fragColor: vec4<f32>,
    @location(1) fragNormal: vec3<f32>,
};

// All fragments use the same texture/sampler layout
@group(2) @binding(0) var texture0: texture_2d<f32>;
@group(2) @binding(1) var textureSampler: sampler;

@fragment
fn main(input: PSInput) -> @location(0) vec4<f32>
{
    let lightDir = normalize(vec3<f32>(1.0, -1.0, 1.0));
    let ambientFactor = 0.1;
    let diff = max(-dot(input.fragNormal, lightDir), 0.0);
    let diffuse = diff * input.fragColor.rgb;
    let ambient = ambientFactor * input.fragColor.rgb;
    let color = clamp(diffuse + ambient, vec3<f32>(0.0), vec3<f32>(1.0));
    return vec4<f32>(color, input.fragColor.a);
}
