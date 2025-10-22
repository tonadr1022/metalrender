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

struct Data {
    uint d1;
    uint d2;
};

constant constexpr int flipLookup[6] = {1, -1, -1, 1, -1, 1};

constant const float3 normal_lookup[8] = {
  float3(0,1,0),
  float3(0,-1,0),
  float3(1,0,0),
  float3(-1,0,0),
  float3(0,0,1),
  float3(0,0,-1)
};

constant const float3 color_lookup[8] = {
  float3(0.2, 0.659, 0.839),
  float3(0.302, 0.302, 0.302),
  float3(0.278, 0.600, 0.141),
  float3(0.1, 0.1, 0.6),
  float3(0.1, 0.6, 0.6),
  float3(0.6, 0.1, 0.6),
  float3(0.6, 0.6, 0.1),
  float3(0.6, 0.1, 0.1)
};

v2f vertex chunk_vertex_main(uint vertexId [[vertex_id]],
                             device const Data* vertices [[buffer(0)]],
                             constant PerChunkUniforms& per_chunk_uniforms [[buffer(1)]],
                             constant Uniforms& uniforms [[buffer(2)]]) {
    uint v_id = vertexId & 3;

    int3 chunk_world_pos = per_chunk_uniforms.chunk_pos.xyz;
    v2f o;
    const Data vert = vertices[vertexId >> 2];
    int3 i_vertex_pos = int3(vert.d1, vert.d1 >> 6u, vert.d1 >> 12u) & 63;
    int face = per_chunk_uniforms.chunk_pos.w;
    int w = int((vert.d1 >> 18u) & 63u), h = int((vert.d1 >> 24u) & 63u);
    uint wDir = (face & 2) >> 1, hDir = 2 - (face >> 2);
    int wMod = v_id >> 1, hMod = v_id & 1;
    i_vertex_pos[wDir] += (w * wMod * flipLookup[face]);
    i_vertex_pos[hDir] += (h * hMod);

    float3 pos_in_chunk = float3(i_vertex_pos);
    uint material = (vert.d2 & 255u) - 1;
    float3 color = color_lookup[material];

    o.position = uniforms.vp * float4(pos_in_chunk.xyz + float3(chunk_world_pos), 1.0);
    o.color = half4(half3(color),1.0);

    //o.color = half4(pos_in_chunk.x / k_chunk_len, pos_in_chunk.y / k_chunk_len, pos_in_chunk.z / k_chunk_len, 1.0);
    return o;
}

float4 fragment chunk_fragment_main(v2f in [[stage_in]]) {
    return float4(in.color);
}
