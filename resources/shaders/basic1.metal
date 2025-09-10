#include <metal_stdlib>
using namespace metal;

struct v2f {
  float4 position [[position]];
  half4 color;
  float2 uv;
  uint material_id;
};

struct Vertex {
    packed_float4 pos;
    packed_float2 uv;
    packed_float3 normal;
};

struct Material {
    texture2d<float> albedo [[id(0)]];
};

struct Uniforms {
    float4x4 model;
    float4x4 vp;
};

struct InstanceData {
    float4x4 model;
};

v2f vertex vertexMain(uint vertexId [[vertex_id]],
                      uint id_instance [[instance_id]],
                      device const Vertex* vertices [[buffer(0)]],
                      constant Uniforms& uniforms [[buffer(1)]],
                      device const InstanceData* instances [[buffer(2)]]) {
    v2f o;
    device const Vertex* vert = vertices + vertexId;
    o.position = uniforms.vp * instances[id_instance].model * float4(vert->pos.xyz, 1.0);
    o.color = half4(half3(vert->normal.xyz) * .5 + .5, 1.0);
    o.material_id = 0;
    o.uv = vert->uv;
    return o;
}

constexpr sampler default_texture_sampler(
    mag_filter::linear,
    min_filter::linear,
    s_address::repeat,
    t_address::repeat,
    r_address::repeat
);

half4 fragment fragmentMain(v2f in [[stage_in]],
                            constant Material& materials [[buffer(0)]]) {
    float4 albedo = materials.albedo.sample(default_texture_sampler, in.uv);
    return half4(albedo);
}
