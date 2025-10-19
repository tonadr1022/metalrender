#include <metal_stdlib>
using namespace metal;

#include "shader_core.h"
#include "shader_global_uniforms.h"
#include "chunk_shaders_shared.h"

struct v2f {
  float4 position [[position]];
  half4 color;
  half3 normal;
  float2 uv;
  uint material_id [[flat]];
};


v2f vertex chunk_vertex_main(uint vertexId [[vertex_id]],
                             device const VoxelVertex* vertices [[buffer(0)]],
                             constant PerChunkUniforms& per_chunk_uniforms [[buffer(1)]],
                             constant Uniforms& uniforms [[buffer(2)]]) {
    float3 chunk_world_pos = per_chunk_uniforms.chunk_pos;
    v2f o;
    device const VoxelVertex& vert = vertices[vertexId];

    o.position = uniforms.vp * float4(vert.pos.xyz + chunk_world_pos, 1.0);
    o.color = half4(vert.pos.x / k_chunk_len, vert.pos.y / k_chunk_len, vert.pos.z / k_chunk_len, 1.0);
    return o;
}

float4 fragment chunk_fragment_main(v2f in [[stage_in]]) {
    return float4(in.color);
}
