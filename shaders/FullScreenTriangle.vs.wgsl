// Fullscreen triangle (no VB).

struct VSOut
{
    @builtin(position) pos : vec4<f32>,
    @location(0)       uv  : vec2<f32>,
};

@vertex
fn main(@builtin(vertex_index) vid : u32) -> VSOut
{
    var o : VSOut;

    // NDC positions for fullscreen triangle.
    var pos : vec2<f32>;
    if (vid == 2u)
    {
        pos = vec2<f32>( 3.0, -1.0);
    }
    else if (vid == 1u)
    {
        pos = vec2<f32>(-1.0,  3.0);
    }
    else
    {
        pos = vec2<f32>(-1.0, -1.0);
    }

    o.pos = vec4<f32>(pos, 0.0, 1.0);

    var uv = pos * 0.5 + vec2<f32>(0.5, 0.5);

    // Optional: flip Y if needed.
    uv.y = 1.0 - uv.y;

    o.uv = uv;
    return o;
}