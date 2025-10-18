#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"

struct DepthReduceArgs {
    uint2 in_dims;
    uint2 out_dims;
};
#define REVERSE_Z 1

#ifdef REVERSE_Z
#define COMP_FUNC min
#define INVALID_DEPTH 1.0
#else 
#define COMP_FUNC max
#define INVALID_DEPTH 1.0
#endif
kernel
void depth_reduce_main(uint2 ti_grid [[thread_position_in_grid]],
                       texture2d<float, access::sample> in_tex [[texture(0)]],
                       texture2d<float, access::write> out_tex [[texture(1)]],
                       constant DepthReduceArgs& args [[buffer(0)]]) {
    uint2 in_dims = args.in_dims;
    uint2 out_dims = args.out_dims;
    if (ti_grid.x >= out_dims.x || ti_grid.y >= out_dims.y) {
        return;
    }
    int2 base = int2(ti_grid * 2u);

    constexpr sampler samp(
        mag_filter::nearest,
        min_filter::nearest,
        address::clamp_to_edge
    );
    auto safe_read = [&](int ox, int oy) -> float {
        int x = base.x + ox;
        int y = base.y + oy;
        if (x < 0 || y < 0 || x >= int(in_dims.x) || y >= int(in_dims.y)) {
            return INVALID_DEPTH;
        }
        return in_tex.sample(samp, (float2(x,y) + float2(0.5)) / float2(in_dims)).r;
    };

    float min_val = safe_read(0,0);
    min_val = COMP_FUNC(min_val, safe_read(0,1));
    min_val = COMP_FUNC(min_val, safe_read(1,0));
    min_val = COMP_FUNC(min_val, safe_read(1,1));

    out_tex.write(min_val, ti_grid);
}
