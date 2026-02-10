struct XForm
{
    modelXform: mat4x4<f32>,
    modelViewProjXform: mat4x4<f32>,
};

struct Material
{
    color: vec4<f32>,
};

struct MaterialBlock
{
    materials: array<Material, 16>,
};

// Uniform buffers must be 16-byte aligned; pad out the rest of the 16 bytes.
struct MaterialIndexBlock
{
    materialIndex: i32,
    _pad0: vec3<i32>,
};

@group(1) @binding(0) var<uniform> xform: XForm;
@group(1) @binding(1) var<uniform> materialBlock: MaterialBlock;
@group(1) @binding(2) var<uniform> materialIndexBlock: MaterialIndexBlock;

struct VSInput
{
    @location(0) inPosition: vec3<f32>,
    @location(1) inNormal: vec3<f32>,
    @location(2) inTexCoord: vec2<f32>,
};

struct VSOutput
{
    @builtin(position) position: vec4<f32>,
    @location(0) fragColor: vec4<f32>,
    @location(1) fragNormal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
};

@vertex
fn main(input: VSInput) -> VSOutput
{
    var output: VSOutput;

    output.position = xform.modelViewProjXform * vec4<f32>(input.inPosition, 1.0);
    output.fragColor = materialBlock.materials[materialIndexBlock.materialIndex].color;
    output.fragNormal = normalize((xform.modelXform * vec4<f32>(input.inNormal, 0.0)).xyz);
    output.texCoord = input.inTexCoord;

    return output;
}
