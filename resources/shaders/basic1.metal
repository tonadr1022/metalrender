#include <metal_stdlib>
using namespace metal;

struct v2f {
  float4 position [[position]];
  half4 color;
};

struct Vertex {
    float3 pos;
    float4 color;
};

struct Uniforms {
    float4x4 model;
    float4x4 vp;
};

v2f vertex vertexMain(uint vertexId [[vertex_id]],
                      device const Vertex* positions [[buffer(0)]],
                      device const Uniforms* uniforms [[buffer(1)]]) {
    v2f o;
    o.position = uniforms->vp * uniforms->model * float4(positions[vertexId].pos, 1.0);
    o.color = half4(positions[vertexId].color);
    return o;
}

half4 fragment fragmentMain(v2f in [[stage_in]]) {
    return half4(in.color);
}
