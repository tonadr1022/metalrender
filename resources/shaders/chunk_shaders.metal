#include <metal_stdlib>
using namespace metal;

#include "shader_core.h"
#include "shader_global_uniforms.h"
#include "chunk_shaders_shared.h"

struct UnpackedVertex {
    float3 pos;
    uint material;
};

UnpackedVertex decode_vertex_position(uint vert) {
    UnpackedVertex o; 
    o.pos = packed_float3(float(vert & 0b1111111), float(vert >> 7 & 0b1111111), float(vert >> 14 & 0b1111111));
    o.material = (vert >> 21) & 0b11111111;
    return o;
}

float3 decode_color_216(uint c) {
    c -= 16;
    uint r_scaled = c / 36;
    uint g_scaled = (c % 36) / 6;
    uint b_scaled = c % 6;
    return float3(float(r_scaled) / 6.0, float(g_scaled) / 6.0, float(b_scaled) / 6.0);
}

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
    UnpackedVertex unpacked = decode_vertex_position(vert.vert);
    float3 pos_in_chunk = unpacked.pos;
    float3 color = decode_color_216(unpacked.material);

    o.position = uniforms.vp * float4(pos_in_chunk.xyz + chunk_world_pos, 1.0);
    o.color = half4(half3(color),1.0);
    //o.color = half4(pos_in_chunk.x / k_chunk_len, pos_in_chunk.y / k_chunk_len, pos_in_chunk.z / k_chunk_len, 1.0);
    return o;
}

float4 fragment chunk_fragment_main(v2f in [[stage_in]]) {
    return float4(in.color);
}
