#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"

struct DepthReduceArgs {
    uint2 in_dims;
    uint2 out_dims;
};

kernel
void depth_reduce_main(uint2 ti_grid [[thread_position_in_grid]],
                       depth2d<float, access::read> in_tex [[texture(0)]],
                       texture2d<float, access::write> out_tex [[texture(1)]],
                       constant DepthReduceArgs& args [[buffer(0)]]) {
    if (ti_grid.x >= args.out_dims.x || ti_grid.y >= args.out_dims.y) {
        return;
    }
    uint2 in_dims = args.in_dims;
    uint2 out_dims = args.out_dims;


    uint base_x = (ti_grid.x * in_dims.x) / out_dims.x;
    uint base_y = (ti_grid.y * in_dims.y) / out_dims.y;
    uint2 base = uint2(base_x, base_y);
    uint2 max_dims = args.in_dims - uint2(1,1); 
    float d0 = in_tex.read(min(base + uint2(0,0), max_dims));
    float d1 = in_tex.read(min(base + uint2(0,1), max_dims));
    float d2 = in_tex.read(min(base + uint2(1,0), max_dims));
    float d3 = in_tex.read(min(base + uint2(1,1), max_dims));
    float depth = min(min(d0, d1), min(d2, d3));

    out_tex.write(float4(depth), ti_grid);
}
