// Fullscreen triangle. Samples src texture with sampler.

struct VSOut
{
    @builtin(position) pos : vec4<f32>,
    @location(0)       uv  : vec2<f32>,
};

@group(2) @binding(0) var srcTex : texture_2d<f32>;
@group(2) @binding(1) var srcSmp : sampler;

@fragment
fn main(i : VSOut) -> @location(0) vec4<f32>
{
    return textureSample(srcTex, srcSmp, i.uv);
}
