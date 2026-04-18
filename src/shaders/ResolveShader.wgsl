// Fullscreen triangle. Samples src texture with sampler.

struct VSOut
{
    @builtin(position) pos : vec4<f32>,
    @location(0)       uv  : vec2<f32>,
};

@group(2) @binding(0) var srcTex : texture_2d<f32>;
@group(2) @binding(1) var srcSmp : sampler;

@vertex
fn vs_main(@builtin(vertex_index) vid : u32) -> VSOut
{
    // NDC positions for fullscreen triangle.
    const vsout = array<VSOut, 3>
    (
        VSOut(vec4<f32>(-1.0, -1.0, 0.0, 1.0), vec2<f32>(-1.0 * 0.5 + 0.5, -1.0 * 0.5 + .5)),
        VSOut(vec4<f32>(-1.0, 3.0, 0.0, 1.0), vec2<f32>(-1.0 * 0.5 + 0.5,  3.0 * 0.5 + 0.5)),
        VSOut(vec4<f32>(3.0, -1.0, 0.0, 1.0), vec2<f32>(3.0 * 0.5 + 0.5, -1.0 * 0.5 + 0.5))
    );

    var o = vsout[vid];

    // Optional: flip Y if needed.
    o.uv.y = 1.0 - o.uv.y;

    return o;
}

@fragment
fn fs_main(i : VSOut) -> @location(0) vec4<f32>
{
    return textureSample(srcTex, srcSmp, i.uv);
}
