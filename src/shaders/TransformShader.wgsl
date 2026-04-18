struct MeshTransform
{
    xform : mat4x4<f32>,
};

struct ClipSpaceTransform
{
    xform : mat4x4<f32>,
}

struct Camera
{
    view      : mat4x4<f32>,
    proj      : mat4x4<f32>,
    viewProj  : mat4x4<f32>,
};

@group(0) @binding(0)
var<storage, read> inMats: array<MeshTransform>;

@group(1) @binding(0)
var<storage, read_write> outMats: array<ClipSpaceTransform>;

@group(2) @binding(0)
var<uniform> camera: Camera;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3<u32>)
{
    let i = gid.x;
    let count = arrayLength(&inMats);

    if (i >= count)
    {
        return;
    }

    outMats[i].xform = camera.viewProj * inMats[i].xform;
}