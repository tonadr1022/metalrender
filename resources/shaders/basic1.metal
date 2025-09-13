#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"
#include "default_vertex.h"

struct v2f {
  float4 position [[position]];
  half4 color;
  half3 normal;
  float2 uv;
  uint material_id [[flat]];
};

struct Material {
    int albedo_tex;
    int normal_tex;
};

struct SceneResourcesBuf {
    array<texture2d<float>, k_max_materials> textures [[id(0)]];
};

#define RENDER_MODE_DEFAULT 0
#define RENDER_MODE_NORMALS 1
#define RENDER_MODE_NORMAL_MAP 2

struct Uniforms {
    float4x4 vp;
    uint32_t render_mode;
};

struct InstanceModel {
    float4x4 model;
};

struct InstanceMaterialId {
    uint mat_id;
};

v2f vertex vertexMain(uint vertexId [[vertex_id]],
                      device const DefaultVertex* vertices [[buffer(0)]],
                      constant Uniforms& uniforms [[buffer(1)]],
                      device const InstanceModel* models [[buffer(2)]],
                      device const InstanceMaterialId* instance_materials [[buffer(3)]],
                      uint inst_id [[instance_id]]) {
    v2f o;
    float4x4 model = models[inst_id].model;
    device const DefaultVertex* vert = vertices + vertexId;
    o.position = uniforms.vp * model * float4(vert->pos.xyz, 1.0);
    o.color = half4(half3(vert->normal.xyz) * .5 + .5, 1.0);
    o.material_id = instance_materials[inst_id].mat_id;
    o.normal = half3(vert->normal);
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

float4 fragment fragmentMain(v2f in [[stage_in]],
                            constant SceneResourcesBuf& scene_buf [[buffer(0)]],
                            device const Material* materials [[buffer(1)]],
                            constant Uniforms& uniforms [[buffer(2)]]) {
    uint render_mode = uniforms.render_mode;
    float4 out_color = float4(0.0);
    if (render_mode == RENDER_MODE_DEFAULT) {
        int albedo_idx = materials[in.material_id].albedo_tex;
        float4 albedo = scene_buf.textures[albedo_idx].sample(default_texture_sampler, in.uv);
        if (albedo.a < 0.5) {
            discard_fragment();
        } else {
            out_color = albedo;
        }
    } else if (render_mode == RENDER_MODE_NORMALS) {
        out_color = float4(float3(in.normal * 0.5 + 0.5), 1.0);
    } else if (render_mode == RENDER_MODE_NORMAL_MAP) {
        int normal_tex_idx = materials[in.material_id].normal_tex;
        float4 normal = scene_buf.textures[normal_tex_idx].sample(default_texture_sampler, in.uv);
        normal.xyz = normal.xyz * 0.5 + 0.5;
        out_color = normal;
    }
    return out_color;
}
