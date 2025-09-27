#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"
#include "types.h"
#include "dispatch_mesh_shared.h"
#include "mesh_shared.h"

struct ICBContainer {
    command_buffer cmd_buf [[id(0)]];
    device const InstanceData* obj_infos [[id(1)]];
};

struct EncodeMeshDrawArgs {
    device const char* main_vertex_buf [[id(EncodeMeshDrawArgs_MainVertexBuf)]];
    device const char* meshlet_buf [[id(EncodeMeshDrawArgs_MeshletBuf)]];
    device const char* instance_model_matrix_buf [[id(EncodeMeshDrawArgs_InstanceModelMatrixBuf)]];
    device const char* instance_data_buf [[id(EncodeMeshDrawArgs_InstanceDataBuf)]];;
    device const char* meshlet_vertices_buf [[id(EncodeMeshDrawArgs_MeshletVerticesBuf)]];
    device const char* meshlet_triangles_buf [[id(EncodeMeshDrawArgs_MeshletTrianglesBuf)]];
    device const char* uniform_buf [[id(EncodeMeshDrawArgs_MainUniformBuf)]];
    device const char* scene_arg_buf [[id(EncodeMeshDrawArgs_SceneArgBuf)]];
};

kernel
void dispatch_mesh_main(uint object_idx [[thread_position_in_grid]],
                        device ICBContainer *icb_container [[buffer(0)]],
                        device const EncodeMeshDrawArgs& draw_args [[buffer(1)]],
                        constant DispatchMeshParams& params [[buffer(2)]]) {
    if (object_idx >= params.tot_meshes) {
        return;
    }
    device const InstanceData& instance_data = icb_container->obj_infos[object_idx];
    const uint num_meshlets = instance_data.meshlet_count;
    const uint threads_per_object_thread_group = 128;
    const uint thread_groups_per_object =
            (num_meshlets + threads_per_object_thread_group - 1) / threads_per_object_thread_group;
    const uint max_mesh_threads =
            max(k_max_triangles_per_meshlet, k_max_vertices_per_meshlet);
    const uint threads_per_mesh_thread_group = max_mesh_threads;
    render_command cmd(icb_container->cmd_buf, object_idx);

    cmd.set_mesh_buffer(draw_args.main_vertex_buf, 0);
    cmd.set_mesh_buffer(draw_args.uniform_buf, 1);
    cmd.set_mesh_buffer(draw_args.meshlet_buf, 2);
    cmd.set_mesh_buffer(draw_args.meshlet_vertices_buf, 3);
    cmd.set_mesh_buffer(draw_args.meshlet_triangles_buf, 4);
    cmd.set_mesh_buffer(draw_args.instance_model_matrix_buf + object_idx * sizeof(float4x4), 5);
    cmd.set_mesh_buffer(draw_args.instance_data_buf + object_idx * sizeof(InstanceData), 6);

    cmd.set_object_buffer(draw_args.instance_data_buf + object_idx * sizeof(InstanceData), 0);

    cmd.set_fragment_buffer(draw_args.scene_arg_buf, 0);
    cmd.set_fragment_buffer(draw_args.uniform_buf, 1);

    cmd.draw_mesh_threadgroups(uint3(thread_groups_per_object, 1, 1),
                               uint3(threads_per_object_thread_group, 1, 1),
                               uint3(threads_per_mesh_thread_group, 1, 1));
}
