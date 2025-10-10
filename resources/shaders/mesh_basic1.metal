#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"
#include "default_vertex.h"
#include "shader_global_uniforms.h"
#include "mesh_shared.h"
#include "math/math.mtl"

#define MESHLET_CULL 1

struct FragArgs {
    uint material_buf_id;
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
    uint meshlet_indices[kMeshThreadgroups];
};

constant bool is_late_pass [[function_constant(0)]];

struct MainObjectArguments {
    device const MeshData* mesh_datas [[id(0)]];
    device const Meshlet* meshlets [[id(1)]];
    device uint* meshlet_vis_buf [[id(2)]];
    texture2d<float, access::sample> depth_pyramid_tex [[id(3)]];
};

[[object, max_total_threadgroups_per_mesh_grid(kMeshThreadgroups)]]
void basic1_object_main(object_data ObjectPayload& out_payload [[payload]],
                        device const InstanceData* instance_data [[buffer(0)]],
                        device const CullData& cull_data [[buffer(1)]],
                        device const MainObjectArguments* args [[buffer(2)]],
                        uint thread_idx [[thread_position_in_threadgroup]],
                        uint tp_grid [[thread_position_in_grid]],
                        mesh_grid_properties grid) {
    device const MeshData& mesh_data = args->mesh_datas[instance_data->mesh_id];
    if (tp_grid >= mesh_data.meshlet_count) {
        return;
    }


    int visible_last_frame = args->meshlet_vis_buf[tp_grid + instance_data->meshlet_vis_base];
    int visible = 1;
    int skip = 0;
    if (!is_late_pass && visible_last_frame == 0) {
        visible = 0;
    }
    if (is_late_pass && visible_last_frame != 0) {
        skip = 1;
    }

    device const Meshlet& meshlet = args->meshlets[tp_grid + mesh_data.meshlet_base];
    float3 world_center = rotate_quat(instance_data->scale * meshlet.center, instance_data->rotation)
                          + instance_data->translation;
    float3 center = (cull_data.view * float4(world_center, 1.0)).xyz;
    float radius = meshlet.radius * instance_data->scale;

    // normal cone culling
    // 8-bit SNORM quantized
    float3 cone_axis = float3(int(meshlet.cone_axis[0]) / 127.0, int(meshlet.cone_axis[1]) / 127.0, int(meshlet.cone_axis[2]) / 127.0);
    cone_axis = rotate_quat(cone_axis, instance_data->rotation);
    cone_axis = float3x3(float3(cull_data.view[0]), float3(cull_data.view[1]), float3(cull_data.view[2])) * cone_axis;
    float cone_cutoff = int(meshlet.cone_cutoff) / 127.0;


    visible = visible && !cone_cull(center, radius, cone_axis, cone_cutoff, float3(0, 0, 0));

    // Ref: https://github.com/zeux/niagara/blob/master/src/shaders/clustercull.comp.glsl#L101C1-L102C102
    // frustum cull, plane symmetry 
    visible = visible && (center.z * cull_data.frustum[1] - abs(center.x) * cull_data.frustum[0]) > -radius;
    visible = visible && (center.z * cull_data.frustum[3] - abs(center.y) * cull_data.frustum[2]) > -radius;
    // z near/far
    visible = visible && ((center.z + radius > cull_data.z_near) || (center.z - radius < cull_data.z_far));

    constexpr sampler samp(
        mag_filter::nearest,
        min_filter::nearest,
        address::clamp_to_edge
    );
    if (is_late_pass && cull_data.meshlet_occlusion_culling_enabled && visible) {
        float4 aabb;
        center.z *= -1; // flip so z is positive
        if (project_sphere(center, radius, cull_data.z_near, cull_data.p00, cull_data.p11, aabb)) {
            float width = (aabb.z - aabb.x) * cull_data.pyramid_width;
            float height = (aabb.w - aabb.y) * cull_data.pyramid_height;
            const uint lod = ceil(log2(max(width, height)));
            const uint2 texSize = uint2(args->depth_pyramid_tex.get_width(0), args->depth_pyramid_tex.get_height(0));
            const uint2 lodSizeInLod0Pixels = texSize & (0xFFFFFFFF << lod);
            const float2 lodScale = float2(texSize) / float2(lodSizeInLod0Pixels);
            const float2 sampleLocationMin = aabb.xy * lodScale;
            const float2 sampleLocationMax = aabb.zw * lodScale;

            const float d0 = args->depth_pyramid_tex.sample(samp, float2(sampleLocationMin.x, sampleLocationMin.y), level(lod)).x;
            const float d1 = args->depth_pyramid_tex.sample(samp, float2(sampleLocationMin.x, sampleLocationMax.y), level(lod)).x;
            const float d2 = args->depth_pyramid_tex.sample(samp, float2(sampleLocationMax.x, sampleLocationMin.y), level(lod)).x;
            const float d3 = args->depth_pyramid_tex.sample(samp, float2(sampleLocationMax.x, sampleLocationMax.y), level(lod)).x;
            float depth = min(min(d0, d1), min(d2, d3));
            float depth_sphere = cull_data.z_near / (center.z - radius);
            visible = visible && depth_sphere > depth;
        }
    }
    visible = visible || (cull_data.paused && visible_last_frame);

    int draw = visible && !skip;
    int payload_idx = simd_prefix_exclusive_sum(draw);
    if (is_late_pass) {
        args->meshlet_vis_buf[tp_grid + instance_data->meshlet_vis_base] = visible;
    }

    if (draw) {
        out_payload.meshlet_indices[payload_idx] = tp_grid;
    }
    uint visible_count = simd_sum(draw);
    if (thread_idx == 0) {
        grid.set_threadgroups_per_grid(uint3(visible_count, 1, 1));
    }
}

struct MeshletPrimitive {
    float4 color [[flat]];
    uint mat_id [[flat]];
};

using OutMeshlet = metal::mesh<MeshletVertex,
                            MeshletPrimitive,
                            k_max_vertices_per_meshlet,
                            k_max_triangles_per_meshlet,
                            topology::triangle
                            >;
[[mesh]]
void basic1_mesh_main(object_data const ObjectPayload& payload [[payload]],
                      device const DefaultVertex* vertices [[buffer(0)]],
                      constant Uniforms& uniforms [[buffer(1)]],
                      device const Meshlet* meshlets [[buffer(2)]],
                      device const uint* meshlet_vertices [[buffer(3)]],
                      device const uchar* meshlet_triangles [[buffer(4)]],
                      device const InstanceData& instance_data [[buffer(6)]],
                      device const MeshData* mesh_datas [[buffer(7)]],
                      uint payload_idx [[threadgroup_position_in_grid]],
                      uint thread_idx [[thread_position_in_threadgroup]],
                      OutMeshlet out_mesh) {
    uint meshlet_idx = payload.meshlet_indices[payload_idx];
    uint instance_id = instance_data.instance_id;
    device const MeshData& mesh_data = mesh_datas[instance_data.mesh_id];
    device const Meshlet& meshlet = meshlets[meshlet_idx + mesh_data.meshlet_base];
    if (thread_idx < meshlet.vertex_count) {
        device const DefaultVertex& vert = vertices[meshlet_vertices[meshlet.vertex_offset + thread_idx + mesh_data.meshlet_vertices_offset]];
        MeshletVertex out_vert;

        float3 world_pos = rotate_quat(instance_data.scale * vert.pos.xyz, instance_data.rotation)
                          + instance_data.translation;
        out_vert.pos = uniforms.vp * float4(world_pos, 1.0);
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
    array<texture2d<float>, k_max_textures> textures [[id(0)]];
    device const Material* materials [[id(k_max_textures)]];
};

[[fragment]]
float4 basic1_fragment_main(FragmentIn in [[stage_in]],
                            device const SceneResourcesBuf& scene_buf [[buffer(0)]],
                            constant Uniforms& uniforms [[buffer(1)]]) {
//    return in.prim.color;
    uint render_mode = uniforms.render_mode;
    float4 out_color = float4(0.0);
    device const Material* material = &scene_buf.materials[in.prim.mat_id];
    if (render_mode == RENDER_MODE_DEFAULT) {
        int albedo_idx = material->albedo_tex;
        float4 albedo = scene_buf.textures[albedo_idx].sample(default_texture_sampler, in.vert.uv);
        out_color = albedo;
    } else if (render_mode == RENDER_MODE_NORMALS) {
        out_color = float4(float3(in.vert.normal * 0.5 + 0.5), 1.0);
    } else if (render_mode == RENDER_MODE_NORMAL_MAP) {
        int normal_tex_idx = material->normal_tex;
        float4 normal = scene_buf.textures[normal_tex_idx].sample(default_texture_sampler, in.vert.uv);
        normal.xyz = normal.xyz * 0.5 + 0.5;
        out_color = normal;
    }
    return out_color;
}
