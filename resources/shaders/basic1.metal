#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"
#include "default_vertex.h"
#include "shader_global_uniforms.h"
#include "mesh_shared.h"
#include "math/math.mtl"

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
    device Material* materials [[id(1024)]];
};


v2f vertex vertexMain(uint vertexId [[vertex_id]],
                      device const DefaultVertex* vertices [[buffer(0)]],
                      constant Uniforms& uniforms [[buffer(1)]],
                      device const InstanceData& instance_data [[buffer(2)]]) {
    v2f o;
    device const DefaultVertex* vert = vertices + vertexId;
    float3 world_pos = rotate_quat(instance_data.scale * vert->pos.xyz, instance_data.rotation)
                          + instance_data.translation;
    o.position = uniforms.vp * float4(world_pos, 1.0);
    o.color = half4(half3(vert->normal.xyz) * .5 + .5, 1.0);
    o.material_id = instance_data.mat_id;
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
                            device const SceneResourcesBuf& scene_buf [[buffer(0)]],
                            constant Uniforms& uniforms [[buffer(1)]]) {
    float4 out_color = float4(0.0);
    out_color = float4(float3(in.normal * 0.5 + 0.5), 1.0);
    return out_color;
    uint render_mode = uniforms.render_mode;
    device const Material& material = scene_buf.materials[in.material_id];
    if (render_mode == RENDER_MODE_DEFAULT) {
        int albedo_idx = material.albedo_tex;
        float4 albedo = scene_buf.textures[albedo_idx].sample(default_texture_sampler, in.uv);
        out_color = albedo;
    } else if (render_mode == RENDER_MODE_NORMALS) {
        out_color = float4(float3(in.normal * 0.5 + 0.5), 1.0);
    } else if (render_mode == RENDER_MODE_NORMAL_MAP) {
        int normal_tex_idx = material.normal_tex;
        float4 normal = scene_buf.textures[normal_tex_idx].sample(default_texture_sampler, in.uv);
        normal.xyz = normal.xyz * 0.5 + 0.5;
        out_color = normal;
    } else if (render_mode == RENDER_MODE_UVS) {
        out_color = float4(in.uv,0.0,1.0);
    }
    return out_color;
}
