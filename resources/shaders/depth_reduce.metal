#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"

struct DepthReduceArgs {
    uint2 dims;
};

kernel
void depth_reduce_main(uint2 ti_grid [[thread_position_in_grid]],
                       texture2d<float, access::read> in_tex [[texture(0)]],
                       texture2d<float, access::write> out_tex [[texture(1)]],
                       constant DepthReduceArgs& args [[buffer(0)]]) {
    if (ti_grid.x >= args.dims.x || ti_grid.y >= args.dims.y) {
        return;
    }

    float d0 = in_tex.read(ti_grid + uint2(0,0)).r;
    float d1 = in_tex.read(ti_grid + uint2(0,1)).r;
    float d2 = in_tex.read(ti_grid + uint2(1,0)).r;
    float d3 = in_tex.read(ti_grid + uint2(1,1)).r;
    float depth = min(min(d0, d1), min(d2, d3));

    out_tex.write(depth, ti_grid);
}
