struct WorldSpaceXform
{
    xform : mat4x4<f32>,
};

struct ClipSpaceXform
{
    xform : mat4x4<f32>,
}

struct MeshDrawData
{
    transformIndex : u32,
    materialIndex : u32,
};

struct Material
{
    color : vec4<f32>,
    metalness : f32,
    roughness : f32,
    // Align to 16 bytes.
    pad0 : f32,
    pad1 : f32,
};

@group(0) @binding(0) var<storage, read> meshDrawData : array<MeshDrawData>;
@group(0) @binding(1) var<storage, read> worldSpaceArray: array<WorldSpaceXform>;
@group(0) @binding(2) var<storage, read> materials : array<Material>;

@group(1) @binding(0) var<storage, read> clipSpaceArray: array<ClipSpaceXform>;

@group(2) @binding(0) var texture0: texture_2d<f32>;
@group(2) @binding(1) var textureSampler: sampler;

struct VSInput
{
    @location(0) inPosition: vec3<f32>,
    @location(1) inNormal: vec3<f32>,
    @location(2) inTexCoord: vec2<f32>,
};

struct FSInput
{
    @builtin(position) position: vec4<f32>,
    @location(0) fragNormal: vec3<f32>,
    @location(1) texCoord: vec2<f32>,
    @location(2) @interpolate(flat) instanceIndex : u32,
};

@vertex
fn vs_main(input: VSInput, @builtin(instance_index) instance_index: u32) -> FSInput
{
    var output: FSInput;

    let transformIdx = meshDrawData[instance_index].transformIndex;
    let clipXform = clipSpaceArray[transformIdx].xform;
    let worldXform = worldSpaceArray[transformIdx].xform;

    output.position = clipXform * vec4<f32>(input.inPosition, 1.0);
    output.fragNormal = normalize((worldXform * vec4<f32>(input.inNormal, 0.0)).xyz);
    output.texCoord = input.inTexCoord;
    output.instanceIndex = instance_index;

    return output;
}

@fragment
fn fs_main(input: FSInput) -> @location(0) vec4<f32>
{
    let material = materials[meshDrawData[input.instanceIndex].materialIndex];
    let lightDir = normalize(vec3<f32>(1.0, -1.0, 1.0));
    let ambientFactor = 0.1;
    let diff = max(-dot(input.fragNormal, lightDir), 0.0);
    let diffuse = diff * material.color.rgb;
    let ambient = ambientFactor * material.color.rgb;
    let color = clamp(diffuse + ambient, vec3<f32>(0.0), vec3<f32>(1.0));
    let litColor = vec4<f32>(color, material.color.a);
    return litColor * textureSample(texture0, textureSampler, input.texCoord);
}
