#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"

struct DepthReduceArgs {
    uint2 in_dims;
    uint2 out_dims;
};

#define COMP_FUNC min
kernel
void depth_reduce_main(uint2 ti_grid [[thread_position_in_grid]],
                       depth2d<float, access::read> in_tex [[texture(0)]],
                       texture2d<float, access::write> out_tex [[texture(1)]],
                       constant DepthReduceArgs& args [[buffer(0)]]) {
    uint2 in_dims = args.in_dims;
    int2 base = int2(ti_grid * 2u);

    auto safe_read = [&](int ox, int oy) -> float {
        int x = base.x + ox;
        int y = base.y + oy;
        if (x < 0 || y < 0 || x >= int(in_dims.x) || y >= int(in_dims.y)) {
            return FLT_MAX;
        }
        return in_tex.read(uint2(x, y)); 
    };

    float min_val = safe_read(0,0);
    min_val = COMP_FUNC(min_val, safe_read(0,1));
    min_val = COMP_FUNC(min_val, safe_read(1,0));
    min_val = COMP_FUNC(min_val, safe_read(1,1));

    out_tex.write(float4(min_val), ti_grid);
}
