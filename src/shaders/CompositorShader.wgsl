// Fullscreen triangle. Samples src texture with sampler.

struct VSOut
{
    @builtin(position) pos : vec4<f32>,
    @location(0)       uv  : vec2<f32>,
};

@group(0) @binding(0) var srcTex : texture_2d<f32>;
@group(0) @binding(1) var srcSmp : sampler;

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

    // Flip v.
    o.uv.y = 1.0 - o.uv.y;

    return o;
}

fn linear_to_srgb(linear: vec3f) -> vec3f
{
    let lo = linear * 12.92;
    let hi = 1.055 * pow(max(linear, vec3f(0.0)), vec3f(1.0 / 2.4)) - 0.055;

    return select(hi, lo,linear <= vec3f(0.0031308));
}

/// Used when outputting to an sRGB framebuffer.
@fragment
fn fs_main_srgb(i : VSOut) -> @location(0) vec4<f32>
{
    return textureSample(srcTex, srcSmp, i.uv);
}

/// Used when outputting to a non-sRGB framebuffer.
@fragment
fn fs_main_non_srgb(i : VSOut) -> @location(0) vec4<f32>
{
    let p = linear_to_srgb(textureSample(srcTex, srcSmp, i.uv).rgb);
    return vec4<f32>(p, 1.0);
}
