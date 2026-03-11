struct WorldSpaceXform
{
    xform : mat4x4<f32>,
};

struct ClipSpaceXform
{
    xform : mat4x4<f32>,
}

struct ViewProj
{
    xform: mat4x4<f32>,
};

@group(0) @binding(0)
var<storage, read> inMats: array<WorldSpaceXform>;

@group(1) @binding(0)
var<storage, read_write> outMats: array<ClipSpaceXform>;

@group(2) @binding(0)
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

    outMats[i].xform = viewProj.xform * inMats[i].xform;
}