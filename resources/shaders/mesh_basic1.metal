#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"
#include "default_vertex.h"
#include "shader_global_uniforms.h"
#include "mesh_shared.h"

struct MeshletDesc {
    uint vertex_offset;
    uint triangle_offset;
    uint vertex_count;
    uint triangle_count;
};

// https://www.ronja-tutorials.com/post/041-hsv-colorspace/
static float3 hue2rgb(float hue) {
    hue = fract(hue); //only use fractional part of hue, making it loop
    float r = abs(hue * 6 - 3) - 1; //red
    float g = 2 - abs(hue * 6 - 2); //green
    float b = 2 - abs(hue * 6 - 4); //blue
    float3 rgb = float3(r,g,b); //combine components
    rgb = saturate(rgb); //clamp between 0 and 1
    return rgb;
}


struct ObjectPayload {
    uint instance_id;
};

[[object]]
void basic1_object_main(object_data ObjectPayload& out_payload [[payload]],
                        device const InstanceData* instance_data [[buffer(0)]],
                        device const MeshData* mesh_datas [[buffer(1)]], // TODO: increase max in icb
                        uint thread_idx [[thread_position_in_threadgroup]],
                        uint object_idx [[threadgroup_position_in_grid]],
                        mesh_grid_properties grid) {
    if (thread_idx == 0) {
        //out_payload.instance_id = object_idx;
        device const MeshData& mesh_data = mesh_datas[instance_data->mesh_id];
        out_payload.instance_id = instance_data->instance_id;
        grid.set_threadgroups_per_grid(uint3(mesh_data.meshlet_count, 1, 1));
    }
}

struct MeshletPrimitive {
    float4 color [[flat]];
    uint mat_id [[flat]];
};

using Meshlet = metal::mesh<MeshletVertex,
                            MeshletPrimitive,
                            k_max_vertices_per_meshlet,
                            k_max_triangles_per_meshlet,
                            topology::triangle
                            >;
[[mesh]]
void basic1_mesh_main(object_data const ObjectPayload& payload [[payload]],
                      device const DefaultVertex* vertices [[buffer(0)]],
                      constant Uniforms& uniforms [[buffer(1)]],
                      device const MeshletDesc* meshlets [[buffer(2)]],
                      device const uint* meshlet_vertices [[buffer(3)]],
                      device const uchar* meshlet_triangles [[buffer(4)]],
                      device const float4x4& model [[buffer(5)]],
                      device const InstanceData& instance_data [[buffer(6)]],
                      device const MeshData* mesh_datas [[buffer(7)]],
                      uint payload_idx [[threadgroup_position_in_grid]],
                      uint thread_idx [[thread_position_in_threadgroup]],
                      Meshlet out_mesh) {
    uint meshlet_idx = payload_idx;
    uint instance_id = payload.instance_id;
    device const MeshData& mesh_data = mesh_datas[instance_data.mesh_id];
    device const MeshletDesc& meshlet = meshlets[meshlet_idx + mesh_data.meshlet_base];
    if (thread_idx < meshlet.vertex_count) {
        device const DefaultVertex& vert = vertices[meshlet_vertices[meshlet.vertex_offset + thread_idx + mesh_data.meshlet_vertices_offset]];
        MeshletVertex out_vert;
        out_vert.pos = uniforms.vp * model * float4(vert.pos.xyz, 1.0);
        out_vert.uv = vert.uv;
        out_vert.normal = vert.normal;
        out_mesh.set_vertex(thread_idx, out_vert);
    }

    if (thread_idx < meshlet.triangle_count) {
        uint i = thread_idx * 3;
        out_mesh.set_index(i + 0, meshlet_triangles[meshlet.triangle_offset + i + 0 + mesh_data.meshlet_triangles_offset]);
        out_mesh.set_index(i + 1, meshlet_triangles[meshlet.triangle_offset + i + 1 + mesh_data.meshlet_triangles_offset]);
        out_mesh.set_index(i + 2, meshlet_triangles[meshlet.triangle_offset + i + 2 + mesh_data.meshlet_triangles_offset]);
        MeshletPrimitive prim = {
            .mat_id = instance_data.mat_id,
            .color = float4(hue2rgb((meshlet_idx + instance_id) * 1.71f), 1.0),
        };
        out_mesh.set_primitive(thread_idx, prim);
    }

    if (thread_idx == 0) {
        out_mesh.set_primitive_count(meshlet.triangle_count);
    }

}

struct FragmentIn {
    MeshletVertex vert;
    MeshletPrimitive prim [[flat]];
};

constexpr sampler default_texture_sampler(
    mag_filter::linear,
    min_filter::linear,
    s_address::repeat,
    t_address::repeat,
    r_address::repeat
);

struct Material {
    int albedo_tex;
    int normal_tex;
};

struct SceneResourcesBuf {
    array<texture2d<float>, k_max_materials> textures [[id(0)]];
    device Material* materials [[id(1024)]];
};

[[fragment]]
float4 basic1_fragment_main(FragmentIn in [[stage_in]],
                            device const SceneResourcesBuf& scene_buf [[buffer(0)]],
                            constant Uniforms& uniforms [[buffer(1)]]) {
    // return in.prim.color;
    uint render_mode = uniforms.render_mode;
    float4 out_color = float4(0.0);
    device const Material& material = scene_buf.materials[in.prim.mat_id];
    if (render_mode == RENDER_MODE_DEFAULT) {
        int albedo_idx = material.albedo_tex;
        float4 albedo = scene_buf.textures[albedo_idx].sample(default_texture_sampler, in.vert.uv);
        out_color = albedo;
    } else if (render_mode == RENDER_MODE_NORMALS) {
        out_color = float4(float3(in.vert.normal * 0.5 + 0.5), 1.0);
    } else if (render_mode == RENDER_MODE_NORMAL_MAP) {
        int normal_tex_idx = material.normal_tex;
        float4 normal = scene_buf.textures[normal_tex_idx].sample(default_texture_sampler, in.vert.uv);
        normal.xyz = normal.xyz * 0.5 + 0.5;
        out_color = normal;
    }
    return out_color;
}
