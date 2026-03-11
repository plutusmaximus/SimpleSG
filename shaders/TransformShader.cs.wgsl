struct XForm
{
    modelXform: mat4x4<f32>,
    modelViewProjXform: mat4x4<f32>,
};

struct ClipSpaceXform
{
    xform : mat4x4<f32>,
}

struct ViewProj
{
    rhs: mat4x4<f32>,
};

@group(0) @binding(0)
var<storage, read> inMats: array<XForm>;

@group(0) @binding(1)
var<storage, read_write> outMats: array<ClipSpaceXform>;

@group(0) @binding(2)
var<uniform> viewProj: ViewProj;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3<u32>)
{
    let i = gid.x;
    let count = arrayLength(&inMats);

    if (i >= count)
    {
        return;
    }

    outMats[i].xform = viewProj.rhs * inMats[i].modelXform;
}