#include <metal_stdlib>
using namespace metal;

#include "shader_constants.h"

struct ObjectInfo {
    uint num_meshlets [[id(0)]];
};

struct ICBContainer {
    command_buffer cmd_buf [[id(0)]];
    device const ObjectInfo* obj_infos [[id(1)]];
};


kernel
void dispatch_mesh_main(uint object_idx [[thread_position_in_grid]],
                        device ICBContainer *icb_container [[buffer(0)]]) {
    ObjectInfo obj_info = icb_container->obj_infos[object_idx];
    const uint num_meshlets = obj_info.num_meshlets;
    const uint threads_per_object_thread_group = 256;
    const uint thread_groups_per_object =
            (num_meshlets + threads_per_object_thread_group - 1) / threads_per_object_thread_group;
    const uint max_mesh_threads =
            max(k_max_triangles_per_meshlet, k_max_vertices_per_meshlet);
    const uint threads_per_mesh_thread_group = max_mesh_threads;
    render_command cmd(icb_container->cmd_buf, object_idx);
//    cmd.set_fragment_buffer(what buffer);
    cmd.draw_mesh_threadgroups(uint3(thread_groups_per_object, 1, 1),
                               uint3(threads_per_object_thread_group, 1, 1),
                               uint3(threads_per_mesh_thread_group, 1, 1));
}
