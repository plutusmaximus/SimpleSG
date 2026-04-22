struct MeshTransform
{
    xform : mat4x4<f32>,
};

struct ClipSpaceTransform
{
    xform : mat4x4<f32>,
}

struct MeshProperties
{
    center : vec3<f32>,
    radius : f32,
    transformIndex : u32,
    materialIndex : u32,
    pad0 : u32,
    pad1 : u32,
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

struct Camera
{
    view      : mat4x4<f32>,
    proj      : mat4x4<f32>,
    viewProj  : mat4x4<f32>,
};

@group(0) @binding(0) var<storage, read> meshTransforms : array<MeshTransform>;
@group(0) @binding(1) var<storage, read> meshProperties : array<MeshProperties>;
@group(0) @binding(2) var<storage, read> materials : array<Material>;

@group(1) @binding(0) var<storage, read> clipSpaceTransforms : array<ClipSpaceTransform>;
@group(1) @binding(1) var<uniform> camera : Camera;

@group(2) @binding(0) var texture0 : texture_2d<f32>;
@group(2) @binding(1) var textureSampler : sampler;

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

    let transformIdx = meshProperties[instance_index].transformIndex;
    let clipXform = clipSpaceTransforms[transformIdx].xform;
    let meshTransform = meshTransforms[transformIdx].xform;

    output.position = clipXform * vec4<f32>(input.inPosition, 1.0);
    output.fragNormal = normalize((meshTransform * vec4<f32>(input.inNormal, 0.0)).xyz);
    output.texCoord = input.inTexCoord;
    output.instanceIndex = instance_index;

    return output;
}

@fragment
fn fs_main(input: FSInput) -> @location(0) vec4<f32>
{
    let material = materials[meshProperties[input.instanceIndex].materialIndex];
    let lightDir = normalize(vec3<f32>(1.0, -1.0, 1.0));
    let ambientFactor = 0.1;
    let diff = max(-dot(input.fragNormal, lightDir), 0.0);
    let diffuse = diff * material.color.rgb;
    let ambient = ambientFactor * material.color.rgb;
    let color = clamp(diffuse + ambient, vec3<f32>(0.0), vec3<f32>(1.0));
    let litColor = vec4<f32>(color, material.color.a);
    return litColor * textureSample(texture0, textureSampler, input.texCoord);
}

/*struct VSSphereOut
{
    @builtin(position) pos : vec4<f32>,
    @location(0) uv        : vec2<f32>,   // [-1,1]
    @location(1) centerVS  : vec3<f32>,
};

@vertex
fn vs_sphere_main(@builtin(vertex_index) vid : u32) -> VSSphereOut
{
    var out : VSSphereOut;

    let centerVS = (camera.view * vec4<f32>(sphere.centerWorld, 1.0)).xyz;

    let uv = quad(vid);

    // billboard quad in view space
    let posVS = centerVS + vec3<f32>(uv * sphere.radius, 0.0);

    out.pos = camera.proj * vec4<f32>(posVS, 1.0);
    out.uv = uv;
    out.centerVS = centerVS;

    return out;
}
*/