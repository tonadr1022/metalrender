#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"
#include "types.h"
#include "dispatch_mesh_shared.h"
#include "mesh_shared.h"
#include "math/math.mtl"

// #define FRUSTUM_CULL 1

struct ICBContainer {
    command_buffer cmd_buf [[id(0)]];
};

struct EncodeMeshDrawArgs {
    device const char* main_vertex_buf [[id(EncodeMeshDrawArgs_MainVertexBuf)]];
    device const char* meshlet_buf [[id(EncodeMeshDrawArgs_MeshletBuf)]];
    device const InstanceData* instance_data_buf [[id(EncodeMeshDrawArgs_InstanceDataBuf)]];
    device const MeshData* mesh_data_buf [[id(EncodeMeshDrawArgs_MeshDataBuf)]];
    device const char* meshlet_vertices_buf [[id(EncodeMeshDrawArgs_MeshletVerticesBuf)]];
    device const char* meshlet_triangles_buf [[id(EncodeMeshDrawArgs_MeshletTrianglesBuf)]];
    device const char* scene_arg_buf [[id(EncodeMeshDrawArgs_SceneArgBuf)]];
};

kernel
void dispatch_mesh_main(uint object_idx [[thread_position_in_grid]],
                        device ICBContainer *icb_container [[buffer(0)]],
                        device const EncodeMeshDrawArgs& draw_args [[buffer(1)]],
                        constant DispatchMeshParams& params [[buffer(2)]],
                        device const uchar* uniform_buf [[buffer(3)]],
                        device const CullData* cull_data [[buffer(4)]]) {
    if (object_idx >= params.tot_meshes) {
        return;
    }
    device const InstanceData& instance_data = draw_args.instance_data_buf[object_idx];
    if (instance_data.instance_id == UINT_MAX) {
        return;
    }
    device const MeshData& mesh_data = draw_args.mesh_data_buf[instance_data.mesh_id];

#ifdef FRUSTUM_CULL
    // frustum cull
    float3 world_center = rotate_quat(instance_data.scale * mesh_data.center, instance_data.rotation)
                          + instance_data.translation;
    float3 center = (cull_data->view * float4(world_center, 1.0)).xyz;
    float radius = mesh_data.radius * instance_data.scale;
    int passed = 1;
    passed = passed && (center.z * cull_data->frustum[1] - abs(center.x) * cull_data->frustum[0]) > -radius;
    passed = passed && (center.z * cull_data->frustum[3] - abs(center.y) * cull_data->frustum[2]) > -radius;
    if (!passed) {
            return;
    }
#endif // FRUSTUM_CULL

    render_command cmd(icb_container->cmd_buf, object_idx);

    cmd.set_mesh_buffer(draw_args.main_vertex_buf, 0);
    cmd.set_mesh_buffer(uniform_buf, 1);
    cmd.set_mesh_buffer(draw_args.meshlet_buf, 2);
    cmd.set_mesh_buffer(draw_args.meshlet_vertices_buf, 3);
    cmd.set_mesh_buffer(draw_args.meshlet_triangles_buf, 4);
    cmd.set_mesh_buffer(draw_args.instance_data_buf + object_idx, 6);
    cmd.set_mesh_buffer(draw_args.mesh_data_buf, 7);

    cmd.set_object_buffer(draw_args.instance_data_buf + object_idx, 0);
    cmd.set_object_buffer(draw_args.mesh_data_buf, 1);
    cmd.set_object_buffer(draw_args.meshlet_buf, 2);
    cmd.set_object_buffer(cull_data, 3);

    cmd.set_fragment_buffer(draw_args.scene_arg_buf, 0);
    cmd.set_fragment_buffer(uniform_buf, 1);

    const uint num_meshlets = mesh_data.meshlet_count;
    const uint threads_per_object_thread_group = kMeshThreadgroups;
    const uint thread_groups_per_object =
            (num_meshlets + threads_per_object_thread_group - 1) / threads_per_object_thread_group;
    const uint max_mesh_threads =
            max(k_max_triangles_per_meshlet, k_max_vertices_per_meshlet);
    const uint threads_per_mesh_thread_group = max_mesh_threads;
    cmd.draw_mesh_threadgroups(uint3(thread_groups_per_object, 1, 1),
                               uint3(threads_per_object_thread_group, 1, 1),
                               uint3(threads_per_mesh_thread_group, 1, 1));
}
