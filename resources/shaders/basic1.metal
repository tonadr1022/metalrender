#include <metal_stdlib>
using namespace metal;

constexpr constant int k_max_materials = 1024;

struct v2f {
  float4 position [[position]];
  half4 color;
  float2 uv;
  uint material_id [[flat]];
};

struct Vertex {
    packed_float4 pos;
    packed_float2 uv;
    packed_float3 normal;
};

struct Material {
    int albedo_tex;
};

struct SceneResourcesBuf {
    array<texture2d<float>, k_max_materials> textures [[id(0)]];
    device const Material* materials [[id(1024)]];
};

struct Uniforms {
    float4x4 model;
    float4x4 vp;
};

v2f vertex vertexMain(uint vertexId [[vertex_id]],
                      device const Vertex* vertices [[buffer(0)]],
                      constant Uniforms& uniforms [[buffer(1)]],
                      constant int& mat_id [[buffer(2)]]) {
    v2f o;
    device const Vertex* vert = vertices + vertexId;
    o.position = uniforms.vp * uniforms.model * float4(vert->pos.xyz, 1.0);
    o.color = half4(half3(vert->normal.xyz) * .5 + .5, 1.0);
    o.material_id = mat_id;
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
                            constant SceneResourcesBuf& scene_buf [[buffer(0)]]) {
    int albedo_idx = scene_buf.materials[in.material_id].albedo_tex;
    float4 albedo = scene_buf.textures[albedo_idx].sample(default_texture_sampler, in.uv);
    return half4(albedo);
}
