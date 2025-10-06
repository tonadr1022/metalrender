#include <metal_stdlib>
using namespace metal;

struct v2f {
    float4 position [[position]];
    float2 uv;
};

v2f vertex full_screen_tex_vertex_main(uint vertex_id [[vertex_id]]) {
    v2f o;

    if (vertex_id == 0) {
        o.position = float4(-1.0, 3.0, 0.0, 1.0);
        o.uv = float2(0.0, 2.0);
    } else if (vertex_id == 1) {
        o.position = float4(3.0, -1.0, 0.0, 1.0);
        o.uv = float2(2.0, 0.0);
    } else {
        o.position = float4(-1.0, -1.0, 0.0, 1.0);
        o.uv = float2(0.0, 0.0);
    }

    o.uv = float2(o.uv.x, 1.0 - o.uv.y);

    return o;
}

struct Args {
    int mip_level;
};

float4 fragment full_screen_tex_frag_main(v2f in [[stage_in]],
                                          texture2d<float, access::sample> tex [[texture(0)]],
                                          constant Args& args [[buffer(0)]]) {
    constexpr sampler samp(
        mag_filter::linear,
        min_filter::linear
    );

    return tex.sample(samp, in.uv, level(args.mip_level));
}
