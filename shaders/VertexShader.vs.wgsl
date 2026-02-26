struct XForm
{
    modelXform: mat4x4<f32>,
    modelViewProjXform: mat4x4<f32>,
};

@group(0) @binding(0) var<storage> xforms: array<XForm>;

struct VSInput
{
    @location(0) inPosition: vec3<f32>,
    @location(1) inNormal: vec3<f32>,
    @location(2) inTexCoord: vec2<f32>,
};

struct VSOutput
{
    @builtin(position) position: vec4<f32>,
    @location(0) fragNormal: vec3<f32>,
    @location(1) texCoord: vec2<f32>,
};

@vertex
fn main(input: VSInput, @builtin(instance_index) instance_index: u32) -> VSOutput
{
    var output: VSOutput;

    var xform = xforms[instance_index];

    output.position = xform.modelViewProjXform * vec4<f32>(input.inPosition, 1.0);
    output.fragNormal = normalize((xform.modelXform * vec4<f32>(input.inNormal, 0.0)).xyz);
    output.texCoord = input.inTexCoord;

    return output;
}
