#include <metal_stdlib>
using namespace metal;

struct v2f {
  float4 position [[position]];
  half4 color;
};

struct Vertex {
    float3 pos;
};

v2f vertex vertexMain(uint vertexId [[vertex_id]],
                      device const Vertex* positions [[buffer(0)]]) {
    v2f o;
    o.position = float4(positions[vertexId].pos, 1.0);
    o.color = half4(1,1,1,1);
    return o;
}

half4 fragment fragmentMain(v2f in [[stage_in]]) {
    return half4(in.color);
}
