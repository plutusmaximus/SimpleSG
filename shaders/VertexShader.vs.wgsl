struct WorldSpaceXform
{
    xform : mat4x4<f32>,
};

struct ClipSpaceXform
{
    xform : mat4x4<f32>,
}

struct MeshToTransformMap
{
    transformIndex : u32,
};

@group(0) @binding(0) var<storage, read> worldSpaceArray: array<WorldSpaceXform>;
@group(0) @binding(1) var<storage, read> clipSpaceArray: array<ClipSpaceXform>;
@group(0) @binding(2) var<storage, read> meshToTransformMap: array<MeshToTransformMap>;

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

    let transformIdx = meshToTransformMap[instance_index].transformIndex;
    let clipXform = clipSpaceArray[transformIdx].xform;
    let worldXform = worldSpaceArray[transformIdx].xform;

    output.position = clipXform * vec4<f32>(input.inPosition, 1.0);
    output.fragNormal = normalize((worldXform * vec4<f32>(input.inNormal, 0.0)).xyz);
    output.texCoord = input.inTexCoord;

    return output;
}
